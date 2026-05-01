#include "signals.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Signal handling is kept small on purpose.
 * Inside a real signal handler we should only do simple safe operations:
 * set flags, forward signals, and write a short message.
 */

static volatile sig_atomic_t should_exit = 0;
static volatile sig_atomic_t active_child_pid = -1;

static void reap_finished_children(void) {
    int saved_errno = errno;
    int status;

    while (waitpid(-1, &status, WNOHANG) > 0) {
        /* child was collected, nothing else needed here */
    }

    errno = saved_errno;
}

static void cleanup_on_exit(void) {
    pid_t child = (pid_t)active_child_pid;

    if (child > 0) {
        kill(child, SIGTERM);
    }

    reap_finished_children();
}

static void handle_signal(int signo) {
    pid_t child = (pid_t)active_child_pid;

    should_exit = 1;

    if (child > 0) {
        kill(child, signo);
    }

    if (signo == SIGINT) {
        write(STDOUT_FILENO, "\nCtrl+C received. Exiting safely...\n", 35);
    } else if (signo == SIGTERM) {
        write(STDOUT_FILENO, "\nSIGTERM received. Exiting safely...\n", 37);
    } else if (signo == SIGHUP) {
        write(STDOUT_FILENO, "\nSIGHUP received. Exiting safely...\n", 36);
    }
}

void signals_init(void) {
    struct sigaction action;

    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1) {
        perror("sigaction SIGINT");
    }

    if (sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction SIGTERM");
    }

    if (sigaction(SIGHUP, &action, NULL) == -1) {
        perror("sigaction SIGHUP");
    }

    if (atexit(cleanup_on_exit) != 0) {
        fprintf(stderr, "signals: could not register cleanup handler\n");
    }
}

void signals_set_active_child(pid_t pid) {
    active_child_pid = (sig_atomic_t)pid;
}

void signals_clear_active_child(void) {
    active_child_pid = -1;
    reap_finished_children();
}

int signals_exit_requested(void) {
    return should_exit;
}