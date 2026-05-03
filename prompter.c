#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "global_defs.h"
#include "proc_manager.h"
#include "pkg_adapter.h"
#include "env_manager.h"



static int cmd_help(int argc, char *argv[]);
static int cmd_install(int argc, char *argv[]);
static int cmd_remove(int argc, char *argv[]);
static int cmd_create(int argc, char *argv[]);
static int cmd_destroy(int argc, char *argv[]);

struct command {
    const char *name;
    int (*handler)(int argc, char *argv[]);
};

static const struct command COMMANDS[] = {
    {"help", cmd_help},
    {"install", cmd_install},
    {"remove", cmd_remove},
    {"create", cmd_create},
    {"destroy", cmd_destroy}
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
    printf("  install <pkg...>    Prepare dependency package installation\n");
    printf("  remove <pkg...>     Prepare dependency package removal\n");
    printf("  create <name>       Create a temporary dev environment\n");
    printf("  destroy <name>      Destroy a temporary dev environment\n");
    return 0;
}

static int cmd_install(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "install: expected at least one package\n");
        return 2;
    }

    printf("[install] preparing to install:");
    for (int i = 1; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("\n");

    /*
     * dry_run = 1 means we only show the package-manager command.
     * this keeps testing safe and avoids changing the user's system.
     */
    return pkg_install_packages(argc, argv, 1);
}

static int cmd_remove(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "remove: expected at least one package\n");
        return 2;
    }

    printf("[remove] preparing to remove:");
    for (int i = 1; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("\n");

    /*
     * dry_run = 1 means we only show the package-manager command.
     * this keeps testing safe and avoids removing real packages.
     */
    return pkg_remove_packages(argc, argv, 1);
}

static int cmd_create(int argc, char *argv[]) {
    char path[512];

    if (argc != 2) {
        fprintf(stderr, "create: expected exactly one environment name\n");
        return 2;
    }

    ensure_base_dir();
    snprintf(path, sizeof(path), "%s/%s", BASE_DIR, argv[1]);

    if (mkdir(path, 0700) == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "create: environment '%s' already exists\n", argv[1]);
        } else {
            perror("create");
        }
        return 1;
    }

    printf("[create] environment created: %s\n", argv[1]);
    printf("[create] path: %s\n", path);
    return 0;
}

static int cmd_destroy(int argc, char *argv[]) {
    char path[512];

    if (argc != 2) {
        fprintf(stderr, "destroy: expected exactly one environment name\n");
        return 2;
    }

    snprintf(path, sizeof(path), "%s/%s", BASE_DIR, argv[1]);

    if (rmdir(path) == -1) {
        perror("destroy");
        return 1;
    }

    printf("[destroy] environment removed: %s\n", argv[1]);
    return 0;
}

static int run_shell_passthrough(int argc, char *argv[]) {
    char command[1024];
    size_t used = 0;

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return 0;
    }

    command[0] = '\0';

    for (int i = 0; i < argc; i++) {
        int written = snprintf(
            command + used,
            sizeof(command) - used,
            "%s%s",
            i == 0 ? "" : " ",
            argv[i]
        );

        if (written < 0 || (size_t)written >= sizeof(command) - used) {
            fprintf(stderr, "shell passthrough: command too long\n");
            return 2;
        }

        used += (size_t)written;
    }

    return proc_run_shell(command, NULL);
}

static int dispatch_command(const char *cmd, int argc, char *argv[]) {
    size_t command_count = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

    for (size_t i = 0; i < command_count; i++) {
        if (strcmp(COMMANDS[i].name, cmd) == 0) {
            return COMMANDS[i].handler(argc, argv);
        }
    }

    return run_shell_passthrough(argc, argv);
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