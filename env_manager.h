#ifndef SHOCKER_ENV_MANAGER_H
#define SHOCKER_ENV_MANAGER_H
#include <fcntl.h>

typedef enum {
    APT,
    DNF,
    PACMAN,
    APK,
    NONE
} PkgMgr ;

typedef enum {
    ENV_RUNNING,
    ENV_STOPPED,
    ENV_ERROR
} EnvStatus;

/// Used to represent a single shocker environment
typedef struct {
    char name[64];
    /// Absolute path to environment's root directory (must end with / )
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

/// Initializes the .shocker/base/ directory which will be shared by all
/// environments for use when utilizing OverlayFS
/// Returns -1 on fail, 0 on success.
int init_env_base_dir(PkgMgr manager);

/// path includes the environment directory name.
/// So, it is in the format ".shocker/[ENV_NAME]
/// Returns -1 on fail, 0 on success.
int delete_env(const char* path);

/// path includes the environment directory name.
/// So, it is in the format ".shocker/[ENV_NAME]
/// Returns -1 on fail, 0 on success.
int create_env(const char* path, const char* env_name);

void cleanup_env_manager_on_exit();

#endif //SHOCKER_ENV_MANAGER_H
