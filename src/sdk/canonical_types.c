#include "sdk/canonical_types.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

typedef struct type_meta {
    sssdk_type_id id;
    const char* name;
    const char* unit;
    const char* category;
} type_meta;

static const type_meta k_type_table[] = {
#define X(id, name, unit, category) {id, name, unit, category},
#include "sdk/canonical_types.def"
#undef X
};

const char* sssdk_type_name(sssdk_type_id id) {
    for (size_t i = 0; i < sizeof(k_type_table) / sizeof(k_type_table[0]); i++) {
        if (k_type_table[i].id == id) {
            return k_type_table[i].name;
        }
    }
    return NULL;
}

const char* sssdk_type_unit(sssdk_type_id id) {
    for (size_t i = 0; i < sizeof(k_type_table) / sizeof(k_type_table[0]); i++) {
        if (k_type_table[i].id == id) {
            return k_type_table[i].unit;
        }
    }
    return NULL;
}

const char* sssdk_type_category(sssdk_type_id id) {
    for (size_t i = 0; i < sizeof(k_type_table) / sizeof(k_type_table[0]); i++) {
        if (k_type_table[i].id == id) {
            return k_type_table[i].category;
        }
    }
    return NULL;
}

int sssdk_type_parse(const char* name, sssdk_type_id* out) {
    if (!name || !out) {
        return -EINVAL;
    }
    for (size_t i = 0; i < sizeof(k_type_table) / sizeof(k_type_table[0]); i++) {
        if (strcmp(name, k_type_table[i].name) == 0) {
            *out = k_type_table[i].id;
            return 0;
        }
    }
    return -ENOENT;
}

bool sssdk_type_is_valid(sssdk_type_id id) {
    return sssdk_type_name(id) != NULL;
}
