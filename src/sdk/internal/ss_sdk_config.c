#include "sdk/internal/ss_sdk_config.h"
#include "sdk/internal/ss_sdk_config_util.h"

#include <stdlib.h>

static void ss_sdk_config_try_set_string_path(const char *path, const char *env_key)
{
    char value[SS_SDK_PATH_BUFFER_SIZE];
    const char *existing;
    ss_sdk_cfg_status status;

    existing = getenv(env_key);
    if (existing != NULL) {
        return;
    }

    status = ss_sdk_cfg_get_string_from_env_json("SUNSPOTS_CONFIG", path, value, sizeof(value));
    if (status != SS_SDK_CFG_OK) {
        return;
    }

    setenv(env_key, value, 0);
}

static void ss_sdk_config_try_set_bool_path(const char *path, const char *env_key)
{
    const char *existing;
    int value = 0;
    ss_sdk_cfg_status status;

    existing = getenv(env_key);
    if (existing != NULL) {
        return;
    }

    status = ss_sdk_cfg_get_bool_from_env_json("SUNSPOTS_CONFIG", path, &value);
    if (status != SS_SDK_CFG_OK) {
        return;
    }

    setenv(env_key, value ? "1" : "0", 0);
}

void ss_sdk_config_bootstrap_env_from_blob(void)
{
    ss_sdk_config_try_set_string_path("system.sdk.db_dir", SS_SDK_ENV_DB_DIR);
    ss_sdk_config_try_set_string_path("system.sdk.log_level", SS_SDK_ENV_LOG_LEVEL);
    ss_sdk_config_try_set_bool_path("system.sdk.log_mirror_enabled", SS_SDK_ENV_LOG_MIRROR_ENABLED);
    ss_sdk_config_try_set_string_path("system.sdk.log_mirror_path", SS_SDK_ENV_LOG_MIRROR_PATH);
    ss_sdk_config_try_set_string_path("system.sdk.log_mirror_max_bytes", SS_SDK_ENV_LOG_MIRROR_MAX_BYTES);
}
