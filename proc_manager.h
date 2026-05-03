#ifndef SHOCKER_PROC_MANAGER_H
#define SHOCKER_PROC_MANAGER_H

#include <sys/types.h>

int proc_run_foreground(char *const argv[], const char *workdir);
int proc_run_shell(const char *command, const char *workdir);

pid_t proc_spawn_background(char *const argv[], const char *workdir);
int proc_reap_children(void);

int proc_kill_process(pid_t pid);
int proc_kill_group(pid_t pgid);

#endif