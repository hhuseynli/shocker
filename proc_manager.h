#ifndef SHOCKER_PROC_MANAGER_H
#define SHOCKER_PROC_MANAGER_H

#include <sys/types.h>

int proc_run_foreground(const char* env_name, char *const argv[]);
int proc_run_shell(const char* env_name, const char *command);

#endif