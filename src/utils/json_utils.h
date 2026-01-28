#ifndef JSON_UTILS_H
#define JSON_UTILS_H


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


#endif