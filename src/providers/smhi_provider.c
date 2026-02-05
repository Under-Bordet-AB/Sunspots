#include "providers/smhi_provider.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libs/json/cJSON.h"

static sssdk_type_id map_smhi_param(const char* name) {
    if (!name) {
        return SSSDK_TYPE_INVALID;
    }
    if (strcmp(name, "t") == 0) {
        return SSSDK_TYPE_TEMPERATURE_C;
    }
    if (strcmp(name, "ws") == 0) {
        return SSSDK_TYPE_WIND_SPEED_MS;
    }
    if (strcmp(name, "tcc_mean") == 0) {
        return SSSDK_TYPE_CLOUD_COVER_PCT;
    }
    return SSSDK_TYPE_INVALID;
}

static int emit_metric(sssdk_runtime* rt, sssdk_type_id type_id, double value, const char* valid_time,
                       long timestamp) {
    char payload[256];
    int n = snprintf(payload, sizeof(payload), "{\"value\":%.6f,\"valid_time\":\"%s\"}", value,
                     valid_time ? valid_time : "");
    if (n <= 0 || (size_t) n >= sizeof(payload)) {
        return -EOVERFLOW;
    }
    sssdk_record rec = {
        .source = "smhi",
        .type = NULL,
        .type_id = type_id,
        .timestamp = timestamp,
        .payload_json = payload,
    };
    return sssdk_emit_record(rt, &rec);
}

int smhi_emit_from_json(sssdk_runtime* rt, const char* json, long timestamp) {
    if (!rt || !json) {
        return -EINVAL;
    }
    if (timestamp <= 0) {
        timestamp = (long) time(NULL);
    }

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return -EINVAL;
    }

    cJSON* time_series = cJSON_GetObjectItemCaseSensitive(root, "timeSeries");
    if (!cJSON_IsArray(time_series) || cJSON_GetArraySize(time_series) == 0) {
        cJSON_Delete(root);
        return -ENOENT;
    }

    int emitted = 0;
    cJSON* slot = NULL;
    cJSON_ArrayForEach(slot, time_series) {
        cJSON* valid_time = cJSON_GetObjectItemCaseSensitive(slot, "validTime");
        cJSON* params = cJSON_GetObjectItemCaseSensitive(slot, "parameters");
        if (!cJSON_IsArray(params)) {
            continue;
        }
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, params) {
            cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
            cJSON* values = cJSON_GetObjectItemCaseSensitive(item, "values");
            if (!cJSON_IsString(name) || !cJSON_IsArray(values) || cJSON_GetArraySize(values) == 0) {
                continue;
            }
            sssdk_type_id type_id = map_smhi_param(name->valuestring);
            if (type_id == SSSDK_TYPE_INVALID) {
                continue;
            }
            cJSON* v = cJSON_GetArrayItem(values, 0);
            if (!cJSON_IsNumber(v)) {
                continue;
            }
            if (emit_metric(rt, type_id, v->valuedouble, cJSON_IsString(valid_time) ? valid_time->valuestring : "",
                            timestamp) == 0) {
                emitted++;
            }
        }
    }

    cJSON_Delete(root);
    return emitted > 0 ? emitted : -ENOENT;
}
