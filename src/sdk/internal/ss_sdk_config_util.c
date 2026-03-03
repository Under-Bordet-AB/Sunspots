#include "sdk/internal/ss_sdk_config_util.h"

#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "libs/json/cJSON.h"

static ss_sdk_cfg_status ss_sdk_cfg_read_root_from_env(const char *env_name, cJSON **out_root)
{
    const char *blob;

    if (env_name == NULL || out_root == NULL) {
        return SS_SDK_CFG_INVALID_ARG;
    }

    blob = getenv(env_name);
    if (blob == NULL || blob[0] == '\0') {
        return SS_SDK_CFG_NOT_FOUND;
    }

    *out_root = cJSON_Parse(blob);
    if (*out_root == NULL) {
        return SS_SDK_CFG_PARSE_ERR;
    }
    return SS_SDK_CFG_OK;
}

static ss_sdk_cfg_status ss_sdk_cfg_path_next_token(
    const char **io_cursor,
    char *out_token,
    size_t out_token_sz,
    size_t *out_token_len,
    bool *out_has_more);

static ss_sdk_cfg_status ss_sdk_cfg_path_next_token(
    const char **io_cursor,
    char *out_token,
    size_t out_token_sz,
    size_t *out_token_len,
    bool *out_has_more)
{
    const char *start;
    const char *dot;
    size_t token_len;

    if (io_cursor == NULL || *io_cursor == NULL || out_token == NULL || out_token_sz == 0U || out_token_len == NULL ||
        out_has_more == NULL) {
        return SS_SDK_CFG_INVALID_ARG;
    }

    start = *io_cursor;
    if (start[0] == '\0') {
        return SS_SDK_CFG_INVALID_ARG;
    }

    dot = strchr(start, '.');
    token_len = (dot == NULL) ? strlen(start) : (size_t)(dot - start);
    if (token_len == 0U || token_len >= out_token_sz) {
        return SS_SDK_CFG_INVALID_ARG;
    }

    memcpy(out_token, start, token_len);
    out_token[token_len] = '\0';
    *out_token_len = token_len;

    if (dot == NULL) {
        *out_has_more = false;
        *io_cursor = start + token_len;
        return SS_SDK_CFG_OK;
    }

    if (dot[1] == '\0') {
        return SS_SDK_CFG_INVALID_ARG;
    }

    *out_has_more = true;
    *io_cursor = dot + 1;
    return SS_SDK_CFG_OK;
}

static ss_sdk_cfg_status ss_sdk_cfg_find_item(const cJSON *root, const char *path, cJSON **out_item)
{
    const cJSON *cursor;
    const char *token_cursor;
    char token[64];

    if (root == NULL || path == NULL || out_item == NULL || path[0] == '\0') {
        return SS_SDK_CFG_INVALID_ARG;
    }

    cursor = root;
    token_cursor = path;
    for (;;) {
        size_t token_len = 0U;
        bool has_more = false;
        const cJSON *next;
        ss_sdk_cfg_status token_status;

        token_status = ss_sdk_cfg_path_next_token(&token_cursor, token, sizeof(token), &token_len, &has_more);
        if (token_status != SS_SDK_CFG_OK) {
            return token_status;
        }

        next = cJSON_GetObjectItemCaseSensitive((cJSON *)cursor, token);
        if (next == NULL) {
            return SS_SDK_CFG_NOT_FOUND;
        }

        if (!has_more) {
            *out_item = (cJSON *)next;
            return SS_SDK_CFG_OK;
        }

        if (!cJSON_IsObject(next)) {
            return SS_SDK_CFG_TYPE_MISMATCH;
        }
        cursor = next;
    }
}

ss_sdk_cfg_status ss_sdk_cfg_get_bool_from_env_json(const char *env_name, const char *path, int *out_bool)
{
    cJSON *root = NULL;
    cJSON *item = NULL;
    ss_sdk_cfg_status status;

    if (out_bool == NULL) {
        return SS_SDK_CFG_INVALID_ARG;
    }

    status = ss_sdk_cfg_read_root_from_env(env_name, &root);
    if (status != SS_SDK_CFG_OK) {
        return status;
    }

    status = ss_sdk_cfg_find_item(root, path, &item);
    if (status != SS_SDK_CFG_OK) {
        cJSON_Delete(root);
        return status;
    }
    if (!cJSON_IsBool(item)) {
        cJSON_Delete(root);
        return SS_SDK_CFG_TYPE_MISMATCH;
    }

    *out_bool = cJSON_IsTrue(item) ? 1 : 0;
    cJSON_Delete(root);
    return SS_SDK_CFG_OK;
}

ss_sdk_cfg_status ss_sdk_cfg_get_int_from_env_json(const char *env_name, const char *path, int min_v, int max_v, int *out_int)
{
    cJSON *root = NULL;
    cJSON *item = NULL;
    ss_sdk_cfg_status status;
    int value;

    if (out_int == NULL || min_v > max_v) {
        return SS_SDK_CFG_INVALID_ARG;
    }

    status = ss_sdk_cfg_read_root_from_env(env_name, &root);
    if (status != SS_SDK_CFG_OK) {
        return status;
    }

    status = ss_sdk_cfg_find_item(root, path, &item);
    if (status != SS_SDK_CFG_OK) {
        cJSON_Delete(root);
        return status;
    }
    if (!cJSON_IsNumber(item)) {
        cJSON_Delete(root);
        return SS_SDK_CFG_TYPE_MISMATCH;
    }

    value = item->valueint;
    if (value < min_v || value > max_v) {
        cJSON_Delete(root);
        return SS_SDK_CFG_RANGE_ERR;
    }

    *out_int = value;
    cJSON_Delete(root);
    return SS_SDK_CFG_OK;
}

ss_sdk_cfg_status ss_sdk_cfg_get_double_from_env_json(const char *env_name, const char *path, double *out_double)
{
    cJSON *root = NULL;
    cJSON *item = NULL;
    ss_sdk_cfg_status status;

    if (out_double == NULL) {
        return SS_SDK_CFG_INVALID_ARG;
    }

    status = ss_sdk_cfg_read_root_from_env(env_name, &root);
    if (status != SS_SDK_CFG_OK) {
        return status;
    }

    status = ss_sdk_cfg_find_item(root, path, &item);
    if (status != SS_SDK_CFG_OK) {
        cJSON_Delete(root);
        return status;
    }
    if (!cJSON_IsNumber(item)) {
        cJSON_Delete(root);
        return SS_SDK_CFG_TYPE_MISMATCH;
    }

    *out_double = item->valuedouble;
    cJSON_Delete(root);
    return SS_SDK_CFG_OK;
}

ss_sdk_cfg_status ss_sdk_cfg_get_string_from_env_json(const char *env_name, const char *path, char *out, size_t out_sz)
{
    cJSON *root = NULL;
    cJSON *item = NULL;
    ss_sdk_cfg_status status;
    size_t n;

    if (out == NULL || out_sz == 0U) {
        return SS_SDK_CFG_INVALID_ARG;
    }

    status = ss_sdk_cfg_read_root_from_env(env_name, &root);
    if (status != SS_SDK_CFG_OK) {
        return status;
    }

    status = ss_sdk_cfg_find_item(root, path, &item);
    if (status != SS_SDK_CFG_OK) {
        cJSON_Delete(root);
        return status;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return SS_SDK_CFG_TYPE_MISMATCH;
    }

    n = strlen(item->valuestring);
    if (n == 0U) {
        cJSON_Delete(root);
        return SS_SDK_CFG_NOT_FOUND;
    }
    if (n >= out_sz) {
        cJSON_Delete(root);
        return SS_SDK_CFG_RANGE_ERR;
    }

    memcpy(out, item->valuestring, n + 1U);
    cJSON_Delete(root);
    return SS_SDK_CFG_OK;
}

ss_sdk_cfg_status ss_sdk_cfg_get_location_from_system_env(ss_sdk_cfg_location *out_location)
{
    ss_sdk_cfg_status status;

    if (out_location == NULL) {
        return SS_SDK_CFG_INVALID_ARG;
    }
    memset(out_location, 0, sizeof(*out_location));

    status = ss_sdk_cfg_get_double_from_env_json("SUNSPOTS_SYSTEM", "location.latitude", &out_location->latitude);
    if (status != SS_SDK_CFG_OK) {
        return status;
    }

    status = ss_sdk_cfg_get_double_from_env_json("SUNSPOTS_SYSTEM", "location.longitude", &out_location->longitude);
    if (status != SS_SDK_CFG_OK) {
        return status;
    }

    (void)ss_sdk_cfg_get_string_from_env_json("SUNSPOTS_SYSTEM", "location.name", out_location->name, sizeof(out_location->name));
    (void)ss_sdk_cfg_get_string_from_env_json(
        "SUNSPOTS_SYSTEM",
        "location.elprisomrade",
        out_location->elprisomrade,
        sizeof(out_location->elprisomrade));

    return SS_SDK_CFG_OK;
}
