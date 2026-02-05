#ifndef WORKER_BOOTSTRAP_H
#define WORKER_BOOTSTRAP_H

#include <stddef.h>

#include "config/config.h"

int worker_cfg_load_from_env(config** out_cfg, char* out_version, size_t out_version_sz);
int worker_cfg_require_keys(const config* cfg, const char* const* required_keys, size_t count);
int worker_cfg_get_common(const config* cfg, const config** out_common);
int worker_cfg_get_section(const config* cfg, const config** out_worker);

#endif
