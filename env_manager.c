#include "env_manager.h"

#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "global_defs.h"
#include "shockerfile.h"
#include "pkg_adapter.h"

#define MAX_ENV_RECORD_COUNT 1000

static int remove_path_recursive(const char *path);
static int remove_directory_contents(const char *path);
static int is_safe_env_delete_path(const char *path);

EnvRecord* env_records[MAX_ENV_RECORD_COUNT];
int env_records_count = 0;
static PkgMgr active_pkg_manager = NONE;

void cleanup_env_manager_on_exit() {
    for (int i = 0; i < env_records_count; i++) {
        free(env_records[i]);
    }
}

void get_os_version(char *version, const size_t len) {
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp == NULL) {
        perror("Unable to open /etc/os-release");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        // Look for the VERSION_ID line
        if (strncmp(line, "VERSION_ID=", 11) == 0) {
            char *value = line + 11;

            // Remove potential quotes and newline
            strtok(value, "\"\n");
            strncpy(version, value, len);
            break;
        }
    }
    fclose(fp);
}


int handle_mkdir_error() {
    if (errno == EEXIST) {
        printf("Error: Environment already exists.\n");
    } else {
        perror("mkdir failed");
    }
    return -1;
}

static int remove_directory_contents(const char *path) {
    DIR *dir = opendir(path);

    if (dir == NULL) {
        perror("opendir failed");
        return -1;
    }

    struct dirent *entry;
    int rc = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_path[1024];
        int written = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (written < 0 || written >= (int) sizeof(child_path)) {
            fprintf(stderr, "delete_env: path too long\n");
            rc = -1;
            break;
        }

        if (remove_path_recursive(child_path) == -1) {
            rc = -1;
            break;
        }
    }

    closedir(dir);
    return rc;
}

static int remove_path_recursive(const char *path) {
    struct stat st;

    if (lstat(path, &st) == -1) {
        perror("stat failed");
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (remove_directory_contents(path) == -1) {
            return -1;
        }

        if (rmdir(path) == -1) {
            perror("rmdir failed");
            return -1;
        }

        return 0;
    }

    if (unlink(path) == -1) {
        perror("unlink failed");
        return -1;
    }

    return 0;
}

static int is_safe_env_delete_path(const char *path) {
    char resolved_base[PATH_MAX];
    char resolved_env_base[PATH_MAX];
    char resolved_target[PATH_MAX];
    size_t base_len;

    if (realpath(BASE_DIR, resolved_base) == NULL) {
        perror("delete_env: resolve BASE_DIR failed");
        return 0;
    }

    if (realpath(path, resolved_target) == NULL) {
        perror("delete_env: resolve target path failed");
        return 0;
    }

    base_len = strlen(resolved_base);
    if (strncmp(resolved_target, resolved_base, base_len) != 0 ||
        (resolved_target[base_len] != '/' && resolved_target[base_len] != '\0')) {
        fprintf(stderr, "delete_env: path must be inside %s\n", BASE_DIR);
        return 0;
    }

    if (strcmp(resolved_target, resolved_base) == 0) {
        fprintf(stderr, "delete_env: refusing to delete base directory\n");
        return 0;
    }

    if (realpath(ENV_BASE_DIR, resolved_env_base) != NULL &&
        strcmp(resolved_target, resolved_env_base) == 0) {
        fprintf(stderr, "delete_env: refusing to delete shared environment base\n");
        return 0;
    }

    return 1;
}

/// 10 or 11 mean success
int init_env_base_dir(PkgMgr manager) {
    char basedir[512];
    char bootstrap_cmd[6000];
    char manager_base_cmd[1024];
    struct stat st = {0};

    snprintf(basedir, sizeof(basedir), "%s/base", BASE_DIR);

    // We check for /bin to see if it's actually bootstrapped, not just an empty folder
    char check_path[560];
    snprintf(check_path, sizeof(check_path), "%s/bin", basedir);

    if (stat(check_path, &st) == 0) {
        printf("Base directory already initialized at: %s\n", basedir);
        return 11;
    }

    printf("Base directory for environments not initialized. Initializing base directory...\n");

    if (stat(basedir, &st) == -1) {
        if (mkdir(basedir, 0755) == -1) {
            perror("mkdir base");
            return -1;
        }
    }

    // Convert basedir to absolute path for package manager commands
    char abs_basedir[PATH_MAX];
    if (realpath(basedir, abs_basedir) == NULL) {
        perror("realpath basedir");
        return -1;
    }

    if (get_minimal_install_command(manager, manager_base_cmd) != 0) {
        fprintf(stderr, "Unsupported or invalid package manager selected.\n");
        return -1;
    }

    active_pkg_manager = manager;

    // Note: Some managers need specific package lists appended

    char curr_os_version[10];
    get_os_version(curr_os_version, sizeof(curr_os_version));
    switch (manager) {
        case DNF:
            // dnf install -y --installroot=/path/to/base --use-host-config --releasever=40 @minimal-environment
            snprintf(bootstrap_cmd, sizeof(bootstrap_cmd),
            "%s%s --releasever=%s "
            "--use-host-config --repo=fedora --repo=updates --nodocs --setopt=install_weak_deps=False @core",
                     manager_base_cmd, abs_basedir, curr_os_version);
            break;
        case APT:
            // debootstrap stable /path/to/base
            snprintf(bootstrap_cmd, sizeof(bootstrap_cmd), "%s %s",
                     manager_base_cmd, abs_basedir);
            break;
        case PACMAN:
            // pacstrap -K -c /path/to/base base
            snprintf(bootstrap_cmd, sizeof(bootstrap_cmd), "%s %s base",
                     manager_base_cmd, abs_basedir);
            break;
        case APK:
            // apk add --initdb --root /path/to/base alpine-base
            snprintf(bootstrap_cmd, sizeof(bootstrap_cmd), "%s %s alpine-base",
                     manager_base_cmd, abs_basedir);
            break;
        default:
            return -1;
    }

    printf("Bootstrapping base directory:\n");
    printf("Executing: %s\n", bootstrap_cmd);
    printf("This may take several minutes depending on your connection...\n");

    int result = system(bootstrap_cmd);
    if (result != 0) {
        fprintf(stderr, "Bootstrap failed with exit code %d\n", result);
        return -1;
    }

    printf("Base environment successfully bootstrapped to %s\n", basedir);
    return 10;
}

int delete_env(const char* path) {
    if (path == NULL || *path == '\0') {
        fprintf(stderr, "delete_env: invalid environment path\n");
        return -1;
    }

    if (!is_safe_env_delete_path(path)) {
        return -1;
    }

    char shockerfile_path[PATH_MAX];
    snprintf(shockerfile_path, sizeof(shockerfile_path), "%s%s", path, SHOCKERFILE_EXTENSION);
    if (does_shockerfile_exist(shockerfile_path)) {
        if (unlink(shockerfile_path) == -1) {
            perror("unlink shockerfile failed");
            return -1;
        }
    }

    if (remove_path_recursive(path) == -1) {
        return -1;
    }

    for (int i = 0; i < env_records_count; i++) {
        if (env_records[i] != NULL && strcmp(env_records[i]->root_path, path) == 0) {
            free(env_records[i]);

            for (int j = i; j < env_records_count - 1; j++) {
                env_records[j] = env_records[j + 1];
            }

            env_records[env_records_count - 1] = NULL;
            env_records_count--;
            break;
        }
    }

    printf("Environment destroyed successfully.\n");
    return 0;
}

int create_env(const char* path, const char* env_name) {
    /*
    if (does_shockerfile_exist(path)) {
        printf("Cannot create new environment: environment %s already exists!", path);
        return -1;
    }
    */
    if (mkdir(path, 0700) == 0) {
        char upperdir[512];
        char workdir[512];
        char mergeddir[512];
        snprintf(upperdir, sizeof(upperdir), "%s/%s", path, "/diff/");
        snprintf(workdir, sizeof(workdir), "%s/%s", path, "/work/");
        snprintf(mergeddir, sizeof(mergeddir), "%s/%s", path, "/merged/");

        if (mkdir(upperdir, 0700) == -1) {return handle_mkdir_error();}
        if (mkdir(workdir, 0700) == -1) {return handle_mkdir_error();}
        if (mkdir(mergeddir, 0700) == -1) {return handle_mkdir_error();}
        printf("Environment created successfully: %s\n", path);
    } else {
        return handle_mkdir_error();
    }

    EnvRecord* env_record = calloc(1, sizeof(EnvRecord));
    if (env_record == NULL) {
        perror("calloc env_record failed");
        return -1;
    }

    snprintf(env_record->name, sizeof(env_record->name), "%s", env_name);
    snprintf(env_record->root_path, sizeof(env_record->root_path), "%s", path);
    env_record->pkg_mgr = active_pkg_manager != NONE ? active_pkg_manager : pkg_detect_manager();
    env_record->created_at = time(NULL);
    env_record->status = ENV_STOPPED;

    env_records[env_records_count++] = env_record;

    serialize_env(env_record);

    return 0;
}
