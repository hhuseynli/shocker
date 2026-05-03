#ifndef SHOCKER_SHOCKERFILE_H
#define SHOCKER_SHOCKERFILE_H

#define SHOCKERFILE_EXTENSION ".shockerfile"

#include "env_manager.h"

static inline const char *status_to_string(EnvStatus status) {
	switch (status) {
		case ENV_RUNNING:
			return "running";
		case ENV_STOPPED:
			return "stopped";
		case ENV_ERROR:
			return "error";
		default:
			return "stopped";
	}
}

int serialize_env(const EnvRecord *env_record);

int deserialize_env(const char *file_path, EnvRecord *env_record);

int does_shockerfile_exist(const char *file_path);

#endif //SHOCKER_SHOCKERFILE_H
