#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include "global_defs.h"
#include "env_manager.h"
#include "shockerfile.h"

/*
 * Example Shockerfile:
 *
     [env]
     name = my-project
     pkg_manager = apt
     packages = gcc, make, libssl-dev, git
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

static void free_packages(char **packages, int pkg_count) {
    if (packages == NULL) {
        return;
    }

    for (int i = 0; i < pkg_count; i++) {
        free(packages[i]);
    }
    free(packages);
}

static int parse_packages(EnvRecord *env_record, const char *packages_value) {
    char *buffer = malloc(strlen(packages_value) + 1);
    if (buffer == NULL) {
        return 1;
    }
    strcpy(buffer, packages_value);

    int capacity = 4;
    int count = 0;
    char **packages = malloc(sizeof(char *) * capacity);
    if (packages == NULL) {
        free(buffer);
        return 1;
    }

    char *token = strtok(buffer, ",");
    while (token != NULL) {
        char *trimmed = trim_whitespace(token);
        if (*trimmed != '\0') {
            if (count == capacity) {
                capacity *= 2;
                char **grown = realloc(packages, sizeof(char *) * capacity);
                if (grown == NULL) {
                    free_packages(packages, count);
                    free(buffer);
                    return 1;
                }
                packages = grown;
            }

            packages[count] = malloc(strlen(trimmed) + 1);
            if (packages[count] == NULL) {
                free_packages(packages, count);
                free(buffer);
                return 1;
            }
            strcpy(packages[count], trimmed);
            count++;
        }

        token = strtok(NULL, ",");
    }

    free(buffer);

    free_packages(env_record->packages, env_record->pkg_count);
    env_record->packages = packages;
    env_record->pkg_count = count;
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

    char pkg_mgr_name[64];
    switch (env_record->pkg_mgr) {
        case APT: strcpy(pkg_mgr_name, "apt"); break;
        case DNF: strcpy(pkg_mgr_name, "dnf"); break;
        case PACMAN: strcpy(pkg_mgr_name, "pacman"); break;
        case APK: strcpy(pkg_mgr_name, "apk"); break;
        default: strcpy(pkg_mgr_name, "unknown");
    }
    fprintf(fp, "pkg_manager = %s\n", pkg_mgr_name);
    fprintf(fp, "created_at = %ld\n", env_record->created_at); // (long) time(NULL)
    fprintf(fp, "packages = ");
    for (int i = 0; i < env_record->pkg_count; i++) {
        if (i == env_record->pkg_count - 1) {
            fprintf(fp,"%s\n", env_record->packages[i]);
        }else {
            fprintf(fp,"%s, ", env_record->packages[i]);
        }
    }

    fclose(fp);

    return 0;
}

/// 1 means file path or env_record is NULL, 2 means file open error, 3 means package names parsing error
int deserialize_env(const char *file_path, EnvRecord *env_record) {
    if (file_path == NULL || env_record == NULL) {
        return 1;
    }

    memset(env_record, 0, sizeof(*env_record));
    env_record->packages = NULL;
    env_record->pkg_count = 0;
    env_record->pkg_mgr = APT;
    env_record->created_at = 0;

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
            }
        } else if (strcmp(key, "packages") == 0) {
            if (parse_packages(env_record, value) != 0) {
                free_packages(env_record->packages, env_record->pkg_count);
                env_record->packages = NULL;
                env_record->pkg_count = 0;
                fclose(fp);
                return 3;
            }
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