#ifndef SS_SDK_CONFIG_UTIL_H
#define SS_SDK_CONFIG_UTIL_H

#include <stddef.h>

typedef enum {
    SS_SDK_CFG_OK = 0,
    SS_SDK_CFG_NOT_FOUND,
    SS_SDK_CFG_PARSE_ERR,
    SS_SDK_CFG_TYPE_MISMATCH,
    SS_SDK_CFG_RANGE_ERR,
    SS_SDK_CFG_INVALID_ARG
} ss_sdk_cfg_status;

typedef struct {
    double latitude;
    double longitude;
    char name[64];
    char elprisomrade[16];
} ss_sdk_cfg_location;

ss_sdk_cfg_status ss_sdk_cfg_get_bool_from_env_json(const char *env_name, const char *path, int *out_bool);
ss_sdk_cfg_status ss_sdk_cfg_get_int_from_env_json(const char *env_name, const char *path, int min_v, int max_v, int *out_int);
ss_sdk_cfg_status ss_sdk_cfg_get_double_from_env_json(const char *env_name, const char *path, double *out_double);
ss_sdk_cfg_status ss_sdk_cfg_get_string_from_env_json(const char *env_name, const char *path, char *out, size_t out_sz);
ss_sdk_cfg_status ss_sdk_cfg_get_location_from_system_env(ss_sdk_cfg_location *out_location);

#endif
