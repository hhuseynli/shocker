#include "pkg_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int command_exists(const char *cmd) {
    char *path_copy;
    char *dir;
    const char *path = getenv("PATH");
    char full_path[512];

    if (cmd == NULL || path == NULL) {
        return 0;
    }

    path_copy = strdup(path);
    if (path_copy == NULL) {
        return 0;
    }

    dir = strtok(path_copy, ":");
    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);

        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return 1;
        }

        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return 0;
}

PackageManager pkg_detect_manager(void) {
    if (command_exists("apt-get")) {
        return PKG_MGR_APT;
    }

    if (command_exists("dnf")) {
        return PKG_MGR_DNF;
    }

    if (command_exists("pacman")) {
        return PKG_MGR_PACMAN;
    }

    if (command_exists("apk")) {
        return PKG_MGR_APK;
    }

    return PKG_MGR_NONE;
}

const char *pkg_manager_name(PackageManager manager) {
    switch (manager) {
        case PKG_MGR_APT:
            return "apt";
        case PKG_MGR_DNF:
            return "dnf";
        case PKG_MGR_PACMAN:
            return "pacman";
        case PKG_MGR_APK:
            return "apk";
        default:
            return "none";
    }
}

static void print_install_command(PackageManager manager, const char *pkg) {
    switch (manager) {
        case PKG_MGR_APT:
            printf("sudo apt-get install -y %s\n", pkg);
            break;
        case PKG_MGR_DNF:
            printf("sudo dnf install -y %s\n", pkg);
            break;
        case PKG_MGR_PACMAN:
            printf("sudo pacman -S --noconfirm %s\n", pkg);
            break;
        case PKG_MGR_APK:
            printf("sudo apk add %s\n", pkg);
            break;
        default:
            printf("no supported package manager found for %s\n", pkg);
            break;
    }
}

static void print_remove_command(PackageManager manager, const char *pkg) {
    switch (manager) {
        case PKG_MGR_APT:
            printf("sudo apt-get remove -y %s\n", pkg);
            break;
        case PKG_MGR_DNF:
            printf("sudo dnf remove -y %s\n", pkg);
            break;
        case PKG_MGR_PACMAN:
            printf("sudo pacman -R --noconfirm %s\n", pkg);
            break;
        case PKG_MGR_APK:
            printf("sudo apk del %s\n", pkg);
            break;
        default:
            printf("no supported package manager found for %s\n", pkg);
            break;
    }
}

int pkg_install_packages(int argc, char *argv[], int dry_run) {
    PackageManager manager = pkg_detect_manager();

    if (argc < 2) {
        fprintf(stderr, "install: expected at least one package\n");
        return 2;
    }

    if (manager == PKG_MGR_NONE) {
        fprintf(stderr, "[install] no supported package manager found\n");
        return 1;
    }

    printf("[pkg] detected package manager: %s\n", pkg_manager_name(manager));

    for (int i = 1; i < argc; i++) {
        if (dry_run) {
            printf("[install] dry-run: ");
            print_install_command(manager, argv[i]);
        } else {
            /*
             * Real installation is intentionally not executed yet.
             * Later this can be connected to proc_manager.c so package commands
             * are executed using fork/exec/waitpid instead of system().
             */
            printf("[install] real execution not enabled yet for: %s\n", argv[i]);
        }
    }

    return 0;
}

int pkg_remove_packages(int argc, char *argv[], int dry_run) {
    PackageManager manager = pkg_detect_manager();

    if (argc < 2) {
        fprintf(stderr, "remove: expected at least one package\n");
        return 2;
    }

    if (manager == PKG_MGR_NONE) {
        fprintf(stderr, "[remove] no supported package manager found\n");
        return 1;
    }

    printf("[pkg] detected package manager: %s\n", pkg_manager_name(manager));

    for (int i = 1; i < argc; i++) {
        if (dry_run) {
            printf("[remove] dry-run: ");
            print_remove_command(manager, argv[i]);
        } else {
            /*
             * real removal is intentionally not executed yet.
             * later this can be connected to proc_manager.c.
             */
            printf("[remove] real execution not enabled yet for: %s\n", argv[i]);
        }
    }

    return 0;
}