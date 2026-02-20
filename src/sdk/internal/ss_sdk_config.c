#include "sdk/internal/ss_sdk_config.h"

#include <stdlib.h>

#include "libs/json/cJSON.h"

static void ss_sdk_config_try_set_string(const cJSON *sdk, const char *json_key, const char *env_key)
{
    const cJSON *item;
    const char *existing;

    existing = getenv(env_key);
    if (existing != NULL) {
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)sdk, json_key);
    if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0') {
        return;
    }

    setenv(env_key, item->valuestring, 0);
}

static void ss_sdk_config_try_set_bool(const cJSON *sdk, const char *json_key, const char *env_key)
{
    const cJSON *item;
    const char *existing;

    existing = getenv(env_key);
    if (existing != NULL) {
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)sdk, json_key);
    if (!cJSON_IsBool(item)) {
        return;
    }

    setenv(env_key, cJSON_IsTrue(item) ? "1" : "0", 0);
}

void ss_sdk_config_bootstrap_env_from_blob(void)
{
    const char *blob;
    cJSON *root = NULL;
    cJSON *common = NULL;
    cJSON *sdk = NULL;

    blob = getenv("SUNSPOTS_CONFIG");
    if (blob == NULL || blob[0] == '\0') {
        return;
    }

    root = cJSON_Parse(blob);
    if (root == NULL) {
        return;
    }

    common = cJSON_GetObjectItemCaseSensitive(root, "common");
    if (cJSON_IsObject(common)) {
        sdk = cJSON_GetObjectItemCaseSensitive(common, "sdk");
    }
    if (!cJSON_IsObject(sdk)) {
        sdk = cJSON_GetObjectItemCaseSensitive(root, "sdk");
    }
    if (!cJSON_IsObject(sdk)) {
        cJSON_Delete(root);
        return;
    }

    ss_sdk_config_try_set_string(sdk, "db_path", SS_SDK_ENV_DB_PATH);
    ss_sdk_config_try_set_string(sdk, "log_level", SS_SDK_ENV_LOG_LEVEL);
    ss_sdk_config_try_set_bool(sdk, "log_mirror_enabled", SS_SDK_ENV_LOG_MIRROR_ENABLED);
    ss_sdk_config_try_set_string(sdk, "log_mirror_path", SS_SDK_ENV_LOG_MIRROR_PATH);

    cJSON_Delete(root);
}
