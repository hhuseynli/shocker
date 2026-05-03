#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "global_defs.h"
#include "proc_manager.h"
#include "pkg_adapter.h"
#include "env_manager.h"
#include "shockerfile.h"


static int cmd_help(int argc, char *argv[]);
static int cmd_create(int argc, char *argv[]);
static int cmd_list(int argc, char *argv[]);
static int cmd_destroy(int argc, char *argv[]);
static int cmd_run(int argc, char *argv[]);


static int has_suffix(const char *value, const char *suffix) {
    size_t value_len;
    size_t suffix_len;

    if (value == NULL || suffix == NULL) {
        return 0;
    }

    value_len = strlen(value);
    suffix_len = strlen(suffix);
    if (value_len < suffix_len) {
        return 0;
    }

    return strcmp(value + value_len - suffix_len, suffix) == 0;
}

static void format_created_at(time_t created_at, char *buffer, size_t buffer_size) {
    struct tm tm_value;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (localtime_r(&created_at, &tm_value) == NULL) {
        snprintf(buffer, buffer_size, "%ld", (long) created_at);
        return;
    }

    if (strftime(buffer, buffer_size, "%Y.%m.%d %H:%M", &tm_value) == 0) {
        snprintf(buffer, buffer_size, "%ld", (long) created_at);
    }
}

struct command {
    const char *name;
    int (*handler)(int argc, char *argv[]);
};

static const struct command COMMANDS[] = {
    {"help", cmd_help},
    {"create", cmd_create},
    {"list", cmd_list},
    {"destroy", cmd_destroy},
    {"run", cmd_run}
};

static void ensure_base_dir(void) {
    struct stat st;

    if (stat(BASE_DIR, &st) == -1) {
        // 0700 -- owner can read, write, execute
        if (mkdir(BASE_DIR, 0700) == -1) {
            perror("mkdir .shocker");
        }
    }
}

static int cmd_help(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("Shocker commands:\n");
    printf("  help                Show this help\n");
    printf("  create <name>       Create a temporary dev environment\n");
    printf("  list                List created environments\n");
    printf("  destroy <name>      Destroy a temporary dev environment\n");
    printf("  run <env>           Run interactive bash in an environment\n");
    return 0;
}

static int cmd_create(int argc, char *argv[]) {
    char path[512];

    if (argc != 2) {
        fprintf(stderr, "create: expected exactly one environment name\n");
        return 2;
    }

    ensure_base_dir();
    snprintf(path, sizeof(path), "%s/%s", BASE_DIR, argv[1]);

    return create_env(path, argv[1]);
}

static int cmd_list(int argc, char *argv[]) {
    DIR *dir;
    struct dirent *entry;
    int listed = 0;

    (void)argv;

    if (argc != 1) {
        fprintf(stderr, "list: expected no arguments\n");
        return 2;
    }

    dir = opendir(BASE_DIR);
    if (dir == NULL) {
        perror("opendir .shocker");
        return 1;
    }

    printf("Name\tCreated At\t\t\tStatus\n");

    while ((entry = readdir(dir)) != NULL) {
        char file_path[512];
        char created_at_display[64];
        EnvRecord env;

        if (!has_suffix(entry->d_name, SHOCKERFILE_EXTENSION)) {
            continue;
        }

        snprintf(file_path, sizeof(file_path), "%s/%s", BASE_DIR, entry->d_name);
        if (deserialize_env(file_path, &env) != 0) {
            fprintf(stderr, "list: skipping unreadable environment file %s\n", entry->d_name);
            continue;
        }

        format_created_at(env.created_at, created_at_display, sizeof(created_at_display));
        printf("%s\t%s (%ld)\t%s\n", env.name, created_at_display, (long) env.created_at, status_to_string(env.status));
        listed++;
    }

    closedir(dir);

    if (listed == 0) {
        printf("No environments found.\n");
    }

    return 0;
}

static int cmd_destroy(int argc, char *argv[]) {
    char path[512];

    if (argc != 2) {
        fprintf(stderr, "destroy: expected exactly one environment name\n");
        return 2;
    }

    snprintf(path, sizeof(path), "%s/%s", BASE_DIR, argv[1]);

    return delete_env(path);
}

static int cmd_run(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "run: expected exactly one environment name\n");
        return 2;
    }

    return proc_run_shell(argv[1], "bash");
}

static int dispatch_command(const char *cmd, int argc, char *argv[]) {
    size_t command_count = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

    for (size_t i = 0; i < command_count; i++) {
        if (strcmp(COMMANDS[i].name, cmd) == 0) {
            return COMMANDS[i].handler(argc, argv);
        }
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    return 2;
}

int main(int argc, char *argv[]) {
    int cmd_index = 0;

    if (argc < 1) {
        fprintf(stderr, "prompter: invalid argument vector\n");
        return 1;
    }

    if (strstr(argv[0], "prompter") != NULL) {
        cmd_index = 1;
    }

    if (argc <= cmd_index) {
        return cmd_help(0, NULL);
    }

    return dispatch_command(argv[cmd_index], argc - cmd_index, argv + cmd_index);
}