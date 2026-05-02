#include "proc_manager.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * This module is responsible for process execution.
 *
 * Current version:
 * - runs foreground commands with fork/exec/waitpid
 * - supports shell passthrough using /bin/sh -c
 * - can spawn simple background commands
 * - can reap finished child processes
 *
 * Later sandboxing work can extend this file with:
 * - unshare()
 * - user/mount/pid namespaces
 * - chroot()
 * - OverlayFS setup
 * - process-group based environment cleanup
 */

static int wait_for_child(pid_t pid) {
    int status = 0;

    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }

        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

int proc_run_foreground(char *const argv[], const char *workdir) {
    pid_t pid;

    if (argv == NULL || argv[0] == NULL) {
        fprintf(stderr, "proc_manager: empty command\n");
        return 2;
    }

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        if (workdir != NULL && chdir(workdir) == -1) {
            perror("chdir");
            _exit(127);
        }

        execvp(argv[0], argv);
        perror("execvp");
        _exit(127);
    }

    return wait_for_child(pid);
}

int proc_run_shell(const char *command, const char *workdir) {
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

    /*
     * For now this runs on the host shell.
     * Later, sandboxing can make this run inside the selected environment root.
     */
    return proc_run_foreground(argv, workdir);
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