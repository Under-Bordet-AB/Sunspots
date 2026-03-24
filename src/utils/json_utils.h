#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdint.h>

#include "cJSON.h"

#define JSON_REQUIRE(cond, err)        \
    do {                               \
        if (!(cond)) return (err);     \
    } while (0)

#define JSON_GET_REQUIRED(obj, key, out, typecheck, err) \
    do {                                                 \
        out = cJSON_GetObjectItem(obj, key);             \
        JSON_REQUIRE(out, err);                          \
        JSON_REQUIRE(typecheck(out), err);               \
    } while (0)

#define JSON_GET_STRING(obj, key, out, err)               \
    do {                                                  \
        cJSON *_tmp;                                      \
        JSON_GET_REQUIRED(obj, key, _tmp, cJSON_IsString, err); \
        out = _tmp->valuestring;                          \
    } while (0)

int add_series_to_json(cJSON* parent, const char* name, const double* values, int series_len, int valid_len) {
    cJSON* array = cJSON_CreateArray();
    if (!array) {
        return -1;
    }

    if (valid_len < 0) valid_len = 0;
    if (valid_len > series_len) valid_len = series_len;

    for (int i = 0; i < series_len; i++) {
        cJSON* value_item;

        if (i >= valid_len) {
            value_item = cJSON_CreateNull();
        } else {
            value_item = cJSON_CreateNumber(values[i]);
        }

        if (!value_item) {
            cJSON_Delete(array);
            return -1;
        }
        cJSON_AddItemToArray(array, value_item);
    }

    cJSON_AddItemToObject(parent, name, array);
    return 0;
}

int add_int64_series_to_json(cJSON* parent, const char* name, const int64_t* values, int series_len,int valid_len) {
    cJSON* array = cJSON_CreateArray();
    if (!array) {
        return -1;
    }

    if (valid_len < 0) valid_len = 0;
    if (valid_len > series_len) valid_len = series_len;

    for (int i = 0; i < series_len; i++) {
        cJSON* value_item;

        if (i >= valid_len) {
            value_item = cJSON_CreateNull();
        } else {
            value_item = cJSON_CreateNumber(values[i]);
        }

        if (!value_item) {
            cJSON_Delete(array);
            return -1;
        }
        cJSON_AddItemToArray(array, value_item);
    }

    cJSON_AddItemToObject(parent, name, array);
    return 0;
}

#endif