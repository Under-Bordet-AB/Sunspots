#include "providers/openmeteo_provider.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libs/json/cJSON.h"

static int emit_metric(sssdk_runtime* rt, sssdk_type_id type_id, double value, long timestamp) {
    char payload[128];
    int n = snprintf(payload, sizeof(payload), "{\"value\":%.6f}", value);
    if (n <= 0 || (size_t) n >= sizeof(payload)) {
        return -EOVERFLOW;
    }
    sssdk_record rec = {
        .source = "openmeteo",
        .type = NULL,
        .type_id = type_id,
        .timestamp = timestamp,
        .payload_json = payload,
    };
    return sssdk_emit_record(rt, &rec);
}

int openmeteo_emit_from_json(sssdk_runtime* rt, const char* json, long timestamp) {
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

    cJSON* current = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (!cJSON_IsObject(current)) {
        cJSON_Delete(root);
        return -ENOENT;
    }

    int emitted = 0;
    cJSON* temperature = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    if (cJSON_IsNumber(temperature) &&
        emit_metric(rt, SSSDK_TYPE_TEMPERATURE_C, temperature->valuedouble, timestamp) == 0) {
        emitted++;
    }

    cJSON* cloud = cJSON_GetObjectItemCaseSensitive(current, "cloud_cover");
    if (cJSON_IsNumber(cloud) && emit_metric(rt, SSSDK_TYPE_CLOUD_COVER_PCT, cloud->valuedouble, timestamp) == 0) {
        emitted++;
    }

    cJSON* radiation = cJSON_GetObjectItemCaseSensitive(current, "shortwave_radiation");
    if (cJSON_IsNumber(radiation) &&
        emit_metric(rt, SSSDK_TYPE_SOLAR_IRRADIANCE_WM2, radiation->valuedouble, timestamp) == 0) {
        emitted++;
    }

    cJSON_Delete(root);
    return emitted > 0 ? emitted : -ENOENT;
}
