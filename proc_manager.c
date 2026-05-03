#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "global_defs.h"
#include "proc_manager.h"

#include <linux/limits.h>

#include "shockerfile.h"

static int setup_id_map(pid_t pid) {
    char path[128], map_data[128];
    int fd;
    uid_t uid = getuid();
    gid_t gid = getgid();

    // Try to disable setgroups for the target pid (may not exist on older kernels)
    snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
    if ((fd = open(path, O_WRONLY)) != -1) {
        if (write(fd, "deny\n", 5) != 5) {
            perror("write setgroups");
            close(fd);
            return -1;
        }
        close(fd);
    } else if (errno != ENOENT) {
        perror("open setgroups");
        return -1;
    }

    snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
    snprintf(map_data, sizeof(map_data), "0 %d 1\n", gid);
    if ((fd = open(path, O_WRONLY)) == -1) {
        perror("open gid_map");
        return -1;
    }
    if (write(fd, map_data, strlen(map_data)) != (ssize_t)strlen(map_data)) {
        perror("write gid_map");
        close(fd);
        return -1;
    }
    close(fd);

    snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
    snprintf(map_data, sizeof(map_data), "0 %d 1\n", uid);
    if ((fd = open(path, O_WRONLY)) == -1) {
        perror("open uid_map");
        return -1;
    }
    if (write(fd, map_data, strlen(map_data)) != (ssize_t)strlen(map_data)) {
        perror("write uid_map");
        close(fd);
        return -1;
    }
    close(fd);

    return 0;
}

/// Creates and mounts the essential files needed for the containerized environment to function properly,
/// especially for package managers like dnf which rely on /proc and /sys.
static int mount_pseudo_fs() {
    // 1. Mount /proc (Essential for process management and dnf)
    if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
        perror("mount proc");
    }

    // 2. Mount /sys (Essential for kernel info)
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) == -1) {
        perror("mount sysfs");
    }

    // 3. Mount /dev as tmpfs (A clean slate for devices)
    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755") == -1) {
        perror("mount /dev tmpfs");
    }

    // 4. Create essential device nodes (or bind mount from host)
    // dnf/gcc often need /dev/null, /dev/random, etc.
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, NULL);

    // 5. DNS - Bind mount host's resolv.conf to the container's /etc/
    // Ensure /etc exists first
    mkdir("/etc", 0755);
    if (mount("/etc/resolv.conf", "/etc/resolv.conf", NULL, MS_BIND, NULL) == -1) {
        // If file doesn't exist to bind over, we touch it first
        FILE* f = fopen("/etc/resolv.conf", "w");
        if (f) { fclose(f); mount("/etc/resolv.conf", "/etc/resolv.conf", NULL, MS_BIND, NULL); }
    }

    // Ensure /tmp is available (Many managers use it for lock files)
    mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");

    // Machine ID (prevents various systemd-related errors)
    if (access("/etc/machine-id", F_OK) == 0) {
        int fd = open("/etc/machine-id", O_CREAT | O_WRONLY, 0444);
        if (fd != -1) close(fd);
        mount("/etc/machine-id", "/etc/machine-id", NULL, MS_BIND | MS_RDONLY, NULL);
    }

    return 0;
}

static int wait_for_child(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) continue;
        return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/// 2 means env doesnt exist
int proc_run_foreground(const char* env_name, char *const argv[]) {
    char shockerfile_path[512];
    snprintf(shockerfile_path, sizeof(shockerfile_path), "%s/%s%s", BASE_DIR, env_name, SHOCKERFILE_EXTENSION);

    if (does_shockerfile_exist(shockerfile_path) == 0) {
        fprintf(stderr, "proc_manager: environment '%s' does not exist\n", env_name);
        return 2;
    }

    char env_path[512];
    snprintf(env_path, sizeof(env_path), "%s/%s", BASE_DIR, env_name);

    pid_t pid;
    char rootfs[1024];
    snprintf(rootfs, sizeof(rootfs), "%s/merged", env_path);

    if (argv == NULL || argv[0] == NULL) return 2;

    int map_ready_pipe[2];
    int cont_pipe[2];

    if (pipe(map_ready_pipe) == -1) {
        perror("pipe");
        return 1;
    }

    if (pipe(cont_pipe) == -1) {
        perror("pipe");
        close(map_ready_pipe[0]); close(map_ready_pipe[1]);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // Child
        // this process will create a new user namespace, notify parent so it can write
        // uid/gid maps, then create new mount and pid namespaces and fork the final worker.
        close(map_ready_pipe[0]);
        close(cont_pipe[1]);

        // 1) Create new user namespace
        if (unshare(CLONE_NEWUSER) == -1) {
            perror("unshare(CLONE_NEWUSER)");
            _exit(127);
        }

        // Notify parent we are ready for uid/gid mapping
        if (write(map_ready_pipe[1], "r", 1) != 1) {
            // best effort
        }

        // Wait for parent to finish mapping
        char ok;
        if (read(cont_pipe[0], &ok, 1) != 1) {
            _exit(127);
        }

        close(map_ready_pipe[1]);
        close(cont_pipe[0]);

        // Create new mount and PID namespaces for the worker
        if (unshare(CLONE_NEWNS | CLONE_NEWPID) == -1) {
            perror("unshare(CLONE_NEWNS|CLONE_NEWPID)");
            _exit(127);
        }

        // Fork again so the grandchild becomes PID 1 in the new PID namespace
        pid_t pid2 = fork();
        if (pid2 < 0) {
            perror("fork");
            _exit(127);
        }

        if (pid2 == 0) {
            // Grandchild: this will run inside new pid and mount namespaces
            if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
                perror("mount propagation private");
                _exit(127);
            }

            // 3. OVERLAY: Mount the filesystem
            char mount_data[1536];
            snprintf(mount_data, sizeof(mount_data), "lowerdir=%s/base,upperdir=%s/diff,workdir=%s/work",
                     BASE_DIR, env_path, env_path);

            if (mount("overlay", rootfs, "overlay", 0, mount_data) == -1) {
                perror("mount overlay");
                _exit(127);
            }

            // 4. PIVOT
            if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) == -1) {
                perror("mount bind");
                _exit(127);
            }

            if (chdir(rootfs) == -1) {
                perror("chdir rootfs");
                _exit(127);
            }

            char old_root_path[PATH_MAX];
            snprintf(old_root_path, sizeof(old_root_path), "%s/old_root", rootfs);
            mkdir(old_root_path, 0777);

            if (syscall(SYS_pivot_root, ".", old_root_path) == -1) {
                perror("pivot_root");
                _exit(127);
            }

            if (chdir("/") == -1) {
                perror("chdir /");
                _exit(127);
            }

            // Unmount and clean up the old host root
            if (umount2(old_root_path, MNT_DETACH) == -1) {
                perror("umount old_root");
                _exit(127);
            }
            rmdir(old_root_path);

            mount_pseudo_fs();

            execvp(argv[0], argv);
            perror("execvp");
            _exit(127);
        } else {
            // Intermediate child: wait for the grandchild and propagate its exit status
            int status = 0;
            while (waitpid(pid2, &status, 0) == -1) {
                if (errno == EINTR) continue;
                _exit(127);
            }
            if (WIFEXITED(status)) _exit(WEXITSTATUS(status));
            if (WIFSIGNALED(status)) _exit(128 + WTERMSIG(status));
            _exit(1);
        }
    } else {
        // Parent
        // wait for child to indicate it unshared user ns, then write uid/gid maps
        close(map_ready_pipe[1]);
        close(cont_pipe[0]);

        // Wait for child's ready signal
        char ready;
        if (read(map_ready_pipe[0], &ready, 1) == 1) {
            // Child has unshared user namespace; write uid/gid maps
            if (setup_id_map(pid) != 0) {
                close(map_ready_pipe[0]);
                close(cont_pipe[1]);
                return 1;
            }
        } else {
            close(map_ready_pipe[0]);
            close(cont_pipe[1]);
            return 1;
        }

        // Tell child it may continue
        if (write(cont_pipe[1], "c", 1) != 1) {
        }

        close(map_ready_pipe[0]);
        close(cont_pipe[1]);

        return wait_for_child(pid);
    }
}

int proc_run_shell(const char* env_name, const char *command) {
    char *const argv[] = {
        "sh",
        "-c",
        (char *)command,
        NULL
    };

    if (command == NULL || command[0] == '\0') {
        fprintf(stderr, "proc_manager: empty shell command\n");
        return 2;
    }

    return proc_run_foreground(env_name, argv);
}

pid_t proc_spawn_background(char *const argv[], const char *workdir) {
    pid_t pid;

    if (argv == NULL || argv[0] == NULL) {
        fprintf(stderr, "proc_manager: empty background command\n");
        return -1;
    }

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            _exit(127);
        }

        if (workdir != NULL && chdir(workdir) == -1) {
            perror("chdir");
            _exit(127);
        }

        execvp(argv[0], argv);
        perror("execvp");
        _exit(127);
    }

    if (setpgid(pid, pid) == -1 && errno != EACCES) {
        perror("setpgid");
    }

    return pid;
}

int proc_reap_children(void) {
    int status = 0;
    int reaped = 0;

    while (waitpid(-1, &status, WNOHANG) > 0) {
        reaped++;
    }

    return reaped;
}

int proc_kill_process(pid_t pid) {
    if (pid <= 0) {
        return 1;
    }

    if (kill(pid, SIGTERM) == -1) {
        perror("kill");
        return 1;
    }

    return 0;
}

int proc_kill_group(pid_t pgid) {
    if (pgid <= 0) {
        return 1;
    }

    if (killpg(pgid, SIGTERM) == -1) {
        perror("killpg");
        return 1;
    }

    return 0;
}