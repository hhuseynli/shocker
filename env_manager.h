#ifndef SHOCKER_ENV_MANAGER_H
#define SHOCKER_ENV_MANAGER_H
#include <time.h>


typedef enum {
    APT,
    DNF,
    PACMAN,
    APK
} PkgMgr ;

typedef enum {
    ENV_RUNNING,
    ENV_STOPPED,
    ENV_ERROR
} EnvStatus;

/// Used to represent a single shocker environment
typedef struct {
    char name[64];
    /// Absolute path to environment's root directory
    char root_path[256];
    /// PID of environment's initial process
    pid_t pid;
    /// Process group ID
    pid_t pgid;
    /// Names of installed packages
    char** packages;
    /// Number of installed packages
    int pkg_count;
    PkgMgr pkg_mgr;
    /// UNIX timestamp of environment creation
    time_t created_at;
    EnvStatus status;
} EnvRecord;

#endif //SHOCKER_ENV_MANAGER_H
