int run_new_env(const char* path);

int run_existing_env(const char* path);

int run_env(const char* path) {
    char shockerfile_path[512];
    snprintf(shockerfile_path, sizeof(shockerfile_path), "%s%s", path, SHOCKERFILE_EXTENSION);
    if (does_shockerfile_exist(shockerfile_path)) {
        return run_existing_env(path);
    }

    return run_new_env(path);
}