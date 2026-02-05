#ifndef SSSDK_CANONICAL_TYPES_H
#define SSSDK_CANONICAL_TYPES_H

#include <stdbool.h>

typedef enum sssdk_type_id {
    SSSDK_TYPE_INVALID = 0,
#define X(id, name, unit, category) id,
#include "sdk/canonical_types.def"
#undef X
    SSSDK_TYPE_COUNT
} sssdk_type_id;

const char* sssdk_type_name(sssdk_type_id id);
const char* sssdk_type_unit(sssdk_type_id id);
const char* sssdk_type_category(sssdk_type_id id);
int sssdk_type_parse(const char* name, sssdk_type_id* out);
bool sssdk_type_is_valid(sssdk_type_id id);

#endif
