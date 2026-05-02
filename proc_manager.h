#ifndef SHOCKER_PROC_MANAGER_H
#define SHOCKER_PROC_MANAGER_H


/// path includes the environment directory name.
/// So, it is in the format ".shocker/[ENV_NAME]
/// Returns -1 on fail, 0 on success.
int run_env(const char* path);

#endif //SHOCKER_PROC_MANAGER_H
