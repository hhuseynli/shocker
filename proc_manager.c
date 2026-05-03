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
#include <termios.h>
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

/// Mounts essential pseudo-filesystems and host files into the overlay rootfs.
/// Must be called after the overlay is mounted but before bind + pivot_root.
static int mount_pseudo_fs_pre(const char *rootfs) {
    char path[PATH_MAX];

    // /proc
    snprintf(path, sizeof(path), "%s/proc", rootfs);
    mkdir(path, 0755);
    if (mount("proc", path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == -1)
        perror("mount proc");

    // /sys — bind from host, read-only
    snprintf(path, sizeof(path), "%s/sys", rootfs);
    mkdir(path, 0755);
    if (mount("/sys", path, NULL, MS_BIND | MS_REC, NULL) == -1)
        perror("mount sys");
    mount(NULL, path, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL);

    // /dev — MS_REC is required because /dev has submounts on the host
    snprintf(path, sizeof(path), "%s/dev", rootfs);
    mkdir(path, 0755);
    if (mount("/dev", path, NULL, MS_BIND | MS_REC, NULL) == -1)
        perror("mount dev");

    // /dev/pts — comes after /dev is mounted
    snprintf(path, sizeof(path), "%s/dev/pts", rootfs);
    mkdir(path, 0755);
    if (mount("devpts", path, "devpts", MS_NOSUID | MS_NOEXEC, NULL) == -1)
        perror("mount devpts");

    // /tmp
    snprintf(path, sizeof(path), "%s/tmp", rootfs);
    mkdir(path, 01777);
    if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") == -1)
        perror("mount tmp");

    // /etc/resolv.conf
    snprintf(path, sizeof(path), "%s/etc", rootfs);
    mkdir(path, 0755);

    /*snprintf(path, sizeof(path), "%s/etc/resolv.conf", rootfs);*/

    int fd = open(path, O_CREAT | O_WRONLY, 0644);
    if (fd != -1) close(fd);

    /*// Bind mount the HOST'S resolv.conf to the CONTAINER'S resolv.conf
    if (mount("/etc/resolv.conf", path, NULL, MS_BIND | MS_RDONLY, NULL) == -1) {
        perror("mount resolv.conf");
    }*/

    // /etc/machine-id
    snprintf(path, sizeof(path), "%s/etc/machine-id", rootfs);
    fd = open(path, O_CREAT | O_WRONLY, 0444);
    if (fd != -1) close(fd);
    if (mount("/etc/machine-id", path, NULL, MS_BIND | MS_RDONLY, NULL) == -1)
        perror("mount machine-id");

    return 0;
}

static int wait_for_child(pid_t pid) {
    int status = 0;

    // Set the child process as the foreground process group so it can receive signals
    if (tcsetpgrp(STDIN_FILENO, pid) == -1) {
        // If tcsetpgrp fails, continue anyway - the child might not need it
        // (e.g., if stdin is not a tty)
    }

    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) continue;
        return 1;
    }

    // Restore the shocker process as the foreground process group
    // This allows input to be received by shocker again after the container exits
    pid_t shocker_pgid = getpgrp();
    if (tcsetpgrp(STDIN_FILENO, shocker_pgid) == -1) {
        // Silently ignore errors - tcsetpgrp may fail if stdin is not a tty
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
        /*close(map_ready_pipe[0]);
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
        */

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

            mount_pseudo_fs_pre(rootfs);

            // 4. PIVOT
            if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) == -1) {
                perror("mount bind");
                _exit(127);
            }

            if (chdir(rootfs) == -1) {
                perror("chdir rootfs");
                _exit(127);
            }

            /*char old_root_path[PATH_MAX];
            snprintf(old_root_path, sizeof(old_root_path), "%s/old_root", rootfs);*/
            mkdir("old_root", 0777);

            if (syscall(SYS_pivot_root, ".", "old_root") == -1) {
                perror("pivot_root");
                _exit(127);
            }

            if (chdir("/") == -1) {
                perror("chdir /");
                _exit(127);
            }

            // automatically configure DNS for dnf install to work
            // 1. Ensure /etc exists
            if (mkdir("/etc", 0755) == -1 && errno != EEXIST) {
                perror("mkdir /etc failed");
            }

            // 2. CRITICAL: Delete existing symlink if it exists
            // This prevents fopen from following the link to a non-existent /run path
            unlink("/etc/resolv.conf");

            // 3. Now write the actual file
            FILE *f = fopen("/etc/resolv.conf", "w");
            if (f) {
                fprintf(f, "nameserver 8.8.8.8\nnameserver 1.1.1.1\n");
                fclose(f);
            } else {
                perror("Failed to write resolv.conf");
            }

            // user namespaces can't have their own sysfs, however some programs/commands may still need it.
            // so we mount our own sys directory and
            // point it to the host's sys
            // we also make our sys directory read-only for safety.
            mkdir("sys", 0755);
            if (mount("/sys", "sys", NULL, MS_BIND | MS_REC, NULL) == -1) {
                perror("bind mount sys");
            }
            mount(NULL, "sys", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL);


            // Unmount and clean up the old host root
            if (umount2("/old_root", MNT_DETACH) == -1) {
                perror("umount old_root");
                _exit(127);
            }
            rmdir("/old_root");

            /*// Save rpm db and dnf state before covering /var
            mkdir("/tmp/.var_lib", 0755);
            if (mount("/var/lib", "/tmp/.var_lib", NULL, MS_BIND | MS_REC, NULL) == -1)
                perror("save /var/lib");

            mkdir("/tmp/.rpm", 0755);
            if (mount("/usr/lib/sysimage/rpm", "/tmp/.rpm", NULL, MS_BIND | MS_REC, NULL) == -1)
                perror("save rpm db");*/

            /*// Cover /var with tmpfs for writable ephemeral directories
            mkdir("/var", 0755);
            if (mount("tmpfs", "/var", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") == -1)
                perror("mount /var");*/
            mkdir("/var/log", 0755);
            mkdir("/var/cache", 0755);
            mkdir("/var/cache/dnf", 0755);
            mkdir("/var/tmp", 01777);
            mkdir("/var/lib", 0755);

            /*
            // /etc/resolv.conf — done post-pivot so the path resolves to the
            // container's own plain file from the base layer, not the host symlink
            mkdir("/etc", 0755);
            int fd = open("/etc/resolv.conf", O_CREAT | O_WRONLY, 0644);
            if (fd != -1) close(fd);
            if (mount("/etc/resolv.conf", "/etc/resolv.conf", NULL, MS_BIND, NULL) == -1) {
                FILE *f = fopen("/etc/resolv.conf", "w");
                if (f) fclose(f);
                mount("/etc/resolv.conf", "/etc/resolv.conf", NULL, MS_BIND, NULL);
            }
            */

            //mount_pseudo_fs();

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
        // Temporarily ignore SIGTTOU when modifying terminal process group
        // This prevents the parent from being suspended during tcsetpgrp
        sigset_t mask, orig_mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTTOU);
        sigprocmask(SIG_BLOCK, &mask, &orig_mask);

        // wait for child to indicate it unshared user ns, then write uid/gid maps
        /*close(map_ready_pipe[1]);
        close(cont_pipe[0]);

        // Wait for child's ready signal
        char ready;
        if (read(map_ready_pipe[0], &ready, 1) == 1) {
            // Child has unshared user namespace; write uid/gid maps
            if (setup_id_map(pid) != 0) {
                close(map_ready_pipe[0]);
                close(cont_pipe[1]);
                sigprocmask(SIG_SETMASK, &orig_mask, NULL);
                return 1;
            }
        } else {
            close(map_ready_pipe[0]);
            close(cont_pipe[1]);
            sigprocmask(SIG_SETMASK, &orig_mask, NULL);
            return 1;
        }

        // Tell child it may continue
        if (write(cont_pipe[1], "c", 1) != 1) {
        }

        close(map_ready_pipe[0]);
        close(cont_pipe[1]);*/

        int result = wait_for_child(pid);

        // Restore the original signal mask
        sigprocmask(SIG_SETMASK, &orig_mask, NULL);

        return result;
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