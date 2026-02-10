#include "core/worker_bootstrap.h"

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

int worker_cfg_load_from_env(config** out_cfg, char* out_version, size_t out_version_sz) {
    if (!out_cfg || !out_version || out_version_sz == 0) {
        return -EINVAL;
    }

    const char* path = getenv("SUNSPOTS_MASTER_CONFIG");
    const char* version = getenv("SUNSPOTS_CONFIG_VERSION");
    if (!path || path[0] == '\0') {
        path = "config/sunspots.json";
    }
    if (!version || version[0] == '\0') {
        version = "master";
    }

    config* cfg = config_create();
    if (!cfg) {
        return -ENOMEM;
    }

    int rc = config_load_file(cfg, path);
    if (rc != 0) {
        config_destroy(&cfg);
        return rc;
    }

    size_t len = strlen(version);
    if (len + 1 > out_version_sz) {
        config_destroy(&cfg);
        return -ENAMETOOLONG;
    }

    memcpy(out_version, version, len + 1);
    *out_cfg = cfg;
    return 0;
}

int worker_cfg_require_keys(const config* cfg, const char* const* required_keys, size_t count) {
    if (!cfg || (!required_keys && count > 0)) {
        return -EINVAL;
    }

    for (size_t i = 0; i < count; i++) {
        const char* key = required_keys[i];
        char buf[8];
        int n = 0;
        bool b = false;
        if (config_get_string(cfg, key, buf, sizeof(buf)) != 0 && config_get_int(cfg, key, &n) != 0 &&
            config_get_bool(cfg, key, &b) != 0) {
            return -ENOENT;
        }
    }
    return 0;
}

int worker_cfg_get_common(const config* cfg, const config** out_common) {
    if (!cfg || !out_common) {
        return -EINVAL;
    }
    const config* c = config_get_subtree(cfg, "common");
    if (!c) {
        return -ENOENT;
    }
    *out_common = c;
    return 0;
}

int worker_cfg_get_section(const config* cfg, const config** out_worker) {
    if (!cfg || !out_worker) {
        return -EINVAL;
    }
    const config* c = config_get_subtree(cfg, "worker");
    if (c) {
        *out_worker = c;
        return 0;
    }

    const char* worker_name = getenv("SUNSPOTS_WORKER_NAME");
    if (!worker_name || worker_name[0] == '\0') {
        return -ENOENT;
    }

    char worker_path[512];
    int n = snprintf(worker_path, sizeof(worker_path), "workers.%s", worker_name);
    if (n <= 0 || (size_t)n >= sizeof(worker_path)) {
        return -ENAMETOOLONG;
    }

    c = config_get_subtree(cfg, worker_path);
    if (!c) {
        return -ENOENT;
    }

    *out_worker = c;
    return 0;
}
