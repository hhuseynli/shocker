#ifndef SHOCKER_SHOCKERFILE_H
#define SHOCKER_SHOCKERFILE_H
#include "env_manager.h"

int serialize_env(const EnvRecord *env_record);

int deserialize_env(const char *file_path, EnvRecord *env_record);

#endif //SHOCKER_SHOCKERFILE_H
