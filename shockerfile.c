#include <stdio.h>
#include <string.h>

#include "env_manager.h"
#include "shockerfile.h"

/*
 * Example Shockerfile:
 *
     [env]
     name = my-project
     pkgmanager = apt
     packages = gcc, make, libssl-dev, git
 *
 *
 */

int serialize_env(const EnvRecord *env_record) {

    char file_path[321];

    snprintf(file_path, sizeof(file_path), "%s%s", env_record->root_path, env_record->name);

    FILE *fp = fopen(file_path, "w");

    if (fp == nullptr) {
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
    fprintf(fp, "pkgmanager = %s\n", pkg_mgr_name);
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