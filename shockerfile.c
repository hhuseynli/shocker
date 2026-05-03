#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include "global_defs.h"
#include "env_manager.h"
#include "pkg_adapter.h"
#include "shockerfile.h"

/*
 * Example Shockerfile:
 *
     [env]
     name = my-project
     pkg_manager = apt
     created_at = 1777573209;
 *
 *
 */

static char *trim_whitespace(char *s) {
    while (*s != '\0' && isspace((unsigned char) *s)) {
        s++;
    }

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char) *(end - 1))) {
        end--;
    }
    *end = '\0';

    return s;
}


static int parse_status(const char *value, EnvStatus *status) {
    if (value == NULL || status == NULL) {
        return 0;
    }

    if (strcmp(value, "running") == 0) {
        *status = ENV_RUNNING;
        return 1;
    }

    if (strcmp(value, "stopped") == 0) {
        *status = ENV_STOPPED;
        return 1;
    }

    if (strcmp(value, "error") == 0) {
        *status = ENV_ERROR;
        return 1;
    }

    return 0;
}

int serialize_env(const EnvRecord *env_record) {

    int file_path_size = 321 + strlen(SHOCKERFILE_EXTENSION); // 256 + 64 + 1 (for \0) + strlen(SHOCKERFILE_EXTENSION)
    char file_path[file_path_size];

    snprintf(file_path, sizeof(file_path), "%s/%s%s", BASE_DIR, env_record->name, SHOCKERFILE_EXTENSION);

    FILE *fp = fopen(file_path, "w");

    if (fp == NULL) {
        perror("Error opening shockerfile");
        return 1;
    }

    fprintf(fp, "[env]\n");
    fprintf(fp, "name = %s\n", env_record->name);

    fprintf(fp, "pkg_manager = %s\n", pkg_manager_name(env_record->pkg_mgr));
    fprintf(fp, "created_at = %ld\n", env_record->created_at); // (long) time(NULL)
    fprintf(fp, "status = %s\n", status_to_string(env_record->status));

    fclose(fp);

    return 0;
}

/// 1 means file path or env_record is NULL, 2 means file open error
int deserialize_env(const char *file_path, EnvRecord *env_record) {
    if (file_path == NULL || env_record == NULL) {
        return 1;
    }

    memset(env_record, 0, sizeof(*env_record));
    env_record->packages = NULL;
    env_record->pkg_count = 0;
    env_record->pkg_mgr = NONE;
    env_record->created_at = 0;
    env_record->status = ENV_STOPPED;

    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
        perror("Error opening shockerfile");
        return 2;
    }

    char line[512];
    int saw_env_section = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *trimmed_line = trim_whitespace(line);

        if (*trimmed_line == '\0' || *trimmed_line == '#' || *trimmed_line == ';') {
            continue;
        }

        if (!saw_env_section) {
            if (strcmp(trimmed_line, "[env]") == 0) {
                saw_env_section = 1;
            }
            continue;
        }

        if (*trimmed_line == '[') {
            break;
        }

        char *equal = strchr(trimmed_line, '=');
        if (equal == NULL) {
            continue;
        }

        *equal = '\0';
        char *key = trim_whitespace(trimmed_line);
        char *value = trim_whitespace(equal + 1);

        if (strcmp(key, "name") == 0) {
            strncpy(env_record->name, value, sizeof(env_record->name) - 1);
            env_record->name[sizeof(env_record->name) - 1] = '\0';
        } else if (strcmp(key, "pkg_manager") == 0) {
            if (strcmp(value, "apt") == 0) {
                env_record->pkg_mgr = APT;
            } else if (strcmp(value, "dnf") == 0) {
                env_record->pkg_mgr = DNF;
            } else if (strcmp(value, "pacman") == 0) {
                env_record->pkg_mgr = PACMAN;
            } else if (strcmp(value, "apk") == 0) {
                env_record->pkg_mgr = APK;
            } else if (strcmp(value, "none") == 0) {
                env_record->pkg_mgr = NONE;
            }
        } else if (strcmp(key, "packages") == 0) {
            continue;
        } else if (strcmp(key, "created_at") == 0) {
            char *end = NULL;
            long long parsed = strtoll(value, &end, 10);
            while (end != NULL && *end != '\0' && isspace((unsigned char) *end)) {
                end++;
            }
            if (end != NULL && *end == ';') {
                end++;
            }
            while (end != NULL && *end != '\0' && isspace((unsigned char) *end)) {
                end++;
            }
            if (end != NULL && *end == '\0') {
                env_record->created_at = (time_t) parsed;
            }
        } else if (strcmp(key, "status") == 0) {
            parse_status(value, &env_record->status);
        }
    }

    fclose(fp);
    return 0;
}

int does_shockerfile_exist(const char *file_path) {
    struct stat st;

    if (file_path == NULL || *file_path == '\0') {
        return 0;
    }

    if (stat(file_path, &st) == -1) {
        return 0;
    }

    return S_ISREG(st.st_mode) ? 1 : 0;
}