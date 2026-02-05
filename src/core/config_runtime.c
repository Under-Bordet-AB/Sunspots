#include "core/config_runtime.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "libs/json/cJSON.h"

static unsigned long djb2_hash(const char* s, unsigned long seed) {
    unsigned long h = seed;
    for (size_t i = 0; s && s[i] != '\0'; i++) {
        h = ((h << 5U) + h) + (unsigned long) (unsigned char) s[i];
    }
    return h;
}

int cfg_runtime_build_one(const config* root, const worker_cfg_target* target, const config* common_cfg,
                          const char* version) {
    if (!root || !target || !target->worker_name || !target->subtree_path || !target->out_file || !common_cfg ||
        !version) {
        return -EINVAL;
    }

    char* common_json = NULL;
    char* worker_json = NULL;
    int rc = config_export_subtree_json(common_cfg, "", &common_json);
    if (rc != 0) {
        return rc;
    }

    rc = config_export_subtree_json(root, target->subtree_path, &worker_json);
    if (rc != 0) {
        cJSON_free(common_json);
        return rc;
    }

    cJSON* out = cJSON_CreateObject();
    cJSON* common_obj = cJSON_Parse(common_json);
    cJSON* worker_obj = cJSON_Parse(worker_json);
    cJSON_free(common_json);
    cJSON_free(worker_json);

    if (!out || !common_obj || !worker_obj) {
        cJSON_Delete(out);
        cJSON_Delete(common_obj);
        cJSON_Delete(worker_obj);
        return -ENOMEM;
    }

    cJSON_AddStringToObject(out, "version", version);
    cJSON_AddItemToObject(out, "common", common_obj);
    cJSON_AddItemToObject(out, "worker", worker_obj);

    char* encoded = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    if (!encoded) {
        return -ENOMEM;
    }

    FILE* f = fopen(target->out_file, "wb");
    if (!f) {
        cJSON_free(encoded);
        return -errno;
    }
    size_t n = fwrite(encoded, 1, strlen(encoded), f);
    int close_rc = fclose(f);
    cJSON_free(encoded);
    if (close_rc != 0 || n == 0) {
        return -EIO;
    }

    return 0;
}

int cfg_runtime_build_all(const config* root, const worker_cfg_target* targets, size_t n,
                          const config* common_cfg, char* out_version, size_t out_version_sz) {
    if (!root || !targets || n == 0 || !common_cfg || !out_version || out_version_sz < 17) {
        return -EINVAL;
    }

    unsigned long hash = 5381UL;
    char* common_json = NULL;
    int rc = config_export_subtree_json(common_cfg, "", &common_json);
    if (rc != 0) {
        return rc;
    }
    hash = djb2_hash(common_json, hash);
    cJSON_free(common_json);
    for (size_t i = 0; i < n; i++) {
        hash = djb2_hash(targets[i].worker_name, hash);
        hash = djb2_hash(targets[i].subtree_path, hash);
        char* worker_json = NULL;
        rc = config_export_subtree_json(root, targets[i].subtree_path, &worker_json);
        if (rc != 0) {
            return rc;
        }
        hash = djb2_hash(worker_json, hash);
        cJSON_free(worker_json);
    }

    int schema_ver = config_get_int_or(common_cfg, "schema.version", 1);
    char schema_buf[32];
    (void) snprintf(schema_buf, sizeof(schema_buf), "%d", schema_ver);
    hash = djb2_hash(schema_buf, hash);
    (void) snprintf(out_version, out_version_sz, "%016lx", hash);

    for (size_t i = 0; i < n; i++) {
        rc = cfg_runtime_build_one(root, &targets[i], common_cfg, out_version);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

bool cfg_runtime_changed(const char* old_version, const char* new_version) {
    if (!old_version || !new_version) {
        return true;
    }
    return strcmp(old_version, new_version) != 0;
}
