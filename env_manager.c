#include "env_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "global_defs.h"
#include "shockerfile.h"
#define MAX_ENV_RECORD_COUNT 1000

EnvRecord* env_records[MAX_ENV_RECORD_COUNT];
int env_records_count = 0;

void cleanup_env_manager_on_exit() {
    for (int i = 0; i < env_records_count; i++) {
        free(env_records[i]);
    }
}

int delete_env(const char* path) {
    char shockerfile_path[512];

    if (path == NULL || *path == '\0') {
        fprintf(stderr, "delete_env: invalid environment path\n");
        return -1;
    }

    snprintf(shockerfile_path, sizeof(shockerfile_path), "%s%s", path, SHOCKERFILE_EXTENSION);

    if (does_shockerfile_exist(shockerfile_path) && unlink(shockerfile_path) == -1) {
        perror("unlink shockerfile failed");
        return -1;
    }

    if (rmdir(path) == -1) {
        perror("rmdir failed");
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
        printf("Environment created successfully: %s\n", path);
    } else {
        if (errno == EEXIST) {
            printf("Error: Environment already exists.\n");
        } else {
            perror("mkdir failed");
        }
        return -1;
    }

    EnvRecord* env_record = malloc(sizeof(EnvRecord));

    snprintf(env_record->name, sizeof(env_record->name), "%s", env_name);
    snprintf(env_record->root_path, sizeof(env_record->root_path), "%s", path);
    env_record->created_at = time(NULL);
    env_record->status = ENV_STOPPED;

    env_records[env_records_count++] = env_record;

    serialize_env(env_record);

    return 0;
}
