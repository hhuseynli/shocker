#ifndef SHOCKER_PKG_ADAPTER_H
#define SHOCKER_PKG_ADAPTER_H

typedef enum {
    PKG_MGR_NONE = 0,
    PKG_MGR_APT,
    PKG_MGR_DNF,
    PKG_MGR_PACMAN,
    PKG_MGR_APK
} PackageManager;

PackageManager pkg_detect_manager(void);
const char *pkg_manager_name(PackageManager manager);

int pkg_install_packages(int argc, char *argv[], int dry_run);
int pkg_remove_packages(int argc, char *argv[], int dry_run);

#endif