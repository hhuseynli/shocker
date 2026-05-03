#ifndef SHOCKER_PKG_ADAPTER_H
#define SHOCKER_PKG_ADAPTER_H
#include "env_manager.h"

PkgMgr pkg_detect_manager(void);
const char *pkg_manager_name(PkgMgr manager);

int pkg_install_packages(int argc, char *argv[], int dry_run);
int pkg_remove_packages(int argc, char *argv[], int dry_run);

/**
 * Populates 'command' with the host-side string needed to bootstrap
 * a minimal OS into the base directory.
 * 
 */
int get_minimal_install_command(PkgMgr manager, char* command);

#endif