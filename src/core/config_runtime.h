#ifndef CONFIG_RUNTIME_H
#define CONFIG_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>

#include "config/config.h"

typedef struct worker_cfg_target {
    const char* worker_name;
    const char* subtree_path;
    const char* out_file;
} worker_cfg_target;

int cfg_runtime_build_one(const config* root, const worker_cfg_target* target, const config* common_cfg,
                          const char* version);
int cfg_runtime_build_all(const config* root, const worker_cfg_target* targets, size_t n, const config* common_cfg,
                          char* out_version, size_t out_version_sz);
bool cfg_runtime_changed(const char* old_version, const char* new_version);

#endif
