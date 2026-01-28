/**
 * @file config.c
 * @brief Configuration module implementation (Subtree-Based / cJSON backend).
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libs/json/cJSON.h"

// Access to environment variables
// environ is provided by unistd.h or declared here if unistd.h doesn't (standard dependent)
// but clang-tidy flags it as redundant if already in unistd.h

// We strictly treat (config*) as (cJSON*)
// This avoids wrapper structs and allows "Subtrees" to be direct pointers.
#define TO_JSON(cfg) ((cJSON*) (cfg))
#define TO_CONST_JSON(cfg) ((const cJSON*) (cfg))
#define TO_CONFIG(json) ((config*) (json))
#define TO_CONST_CONFIG(json) ((const config*) (json))

enum { MAX_CONFIG_SIZE = 1024 * 1024 };  // 1MB limit

// ============================================================================
// Internal Helpers
// ============================================================================

/**
 * @brief Helper to read entire file into memory.
 */
static char* read_file_to_string(const char* path) {
    if (!path)
        return NULL;

    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        (void) fclose(f);
        return NULL;
    }
    long length = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        (void) fclose(f);
        return NULL;
    }

    if (length < 0 || length > MAX_CONFIG_SIZE) {
        (void) fclose(f);
        return NULL;
    }

    char* buf = malloc(sizeof(*buf) * (length + 1));
    if (!buf) {
        (void) fclose(f);
        return NULL;
    }

    memset(buf, 0, length + 1);

    if (fread(buf, 1, length, f) != (size_t) length) {
        free(buf);
        (void) fclose(f);
        return NULL;
    }

    buf[length] = '\0';
    (void) fclose(f);
    return buf;
}

/**
 * @brief Deep merge source JSON into target JSON.
 *        Source items are DETACHED and moved to target.
 */
static void json_merge(cJSON* target, cJSON* source) {
    if (!target || !source)
        return;

    if (!cJSON_IsObject(target) || !cJSON_IsObject(source)) {
        return;
    }

    cJSON* child = source->child;
    cJSON* next = NULL;

    while (child) {
        next = child->next;  // Save next because we might detach child

        // Check if key exists in target
        cJSON* target_child = cJSON_GetObjectItemCaseSensitive(target, child->string);

        if (target_child && cJSON_IsObject(target_child) && cJSON_IsObject(child)) {
            // Both are objects -> Recurse
            json_merge(target_child, child);
        } else {
            // Otherwise, replace/add.
            // We detach from source so source remains valid (but empty/partial)
            cJSON_DetachItemViaPointer(source, child);

            // If it existed, replace it. If not, add it.
            if (target_child) {  // NOLINT(bugprone-branch-clone)
                cJSON_ReplaceItemInObjectCaseSensitive(target, child->string, child);
            } else {
                cJSON_AddItemToObject(target, child->string, child);
            }
        }

        child = next;
    }
}

/**
 * @brief Traverse a dot-notation path (e.g. "modules.server")
 */
static cJSON* resolve_path(const cJSON* root, const char* path) {
    if (!root || !path)
        return NULL;

    // Handle trivial case
    if (strlen(path) == 0)
        return (cJSON*) root;

    // Make a copy to tokenize because strtok_r modifies string
    char* path_copy = strdup(path);
    if (!path_copy)
        return NULL;

    cJSON* current = (cJSON*) root;
    char* saveptr = NULL;
    char* token = strtok_r(path_copy, ".", &saveptr);

    while (token) {
        if (!cJSON_IsObject(current)) {
            current = NULL;
            break;
        }
        current = cJSON_GetObjectItemCaseSensitive(current, token);
        if (!current)
            break;
        token = strtok_r(NULL, ".", &saveptr);
    }

    free(path_copy);
    return current;
}

/**
 * @brief Set a value in a JSON object using a dot-notation path.
 *        Creates intermediate objects as needed.
 */
static void json_set_at_path(cJSON* root, const char* path, cJSON* item) {
    if (!root || !path || !item) {
        if (item)
            cJSON_Delete(item);
        return;
    }

    char* path_copy = strdup(path);
    if (!path_copy) {
        cJSON_Delete(item);
        return;
    }

    cJSON* current = root;
    char* saveptr = NULL;
    char* token = strtok_r(path_copy, ".", &saveptr);
    char* next_token = strtok_r(NULL, ".", &saveptr);

    while (next_token) {
        cJSON* next_obj = cJSON_GetObjectItemCaseSensitive(current, token);
        if (!next_obj || !cJSON_IsObject(next_obj)) {
            // Replace if it exists but is not an object
            if (next_obj) {
                cJSON_DeleteItemFromObjectCaseSensitive(current, token);
            }
            next_obj = cJSON_CreateObject();
            cJSON_AddItemToObject(current, token, next_obj);
        }
        current = next_obj;
        token = next_token;
        next_token = strtok_r(NULL, ".", &saveptr);
    }

    // Set the value at the leaf
    if (cJSON_HasObjectItem(current, token)) {
        cJSON_ReplaceItemInObjectCaseSensitive(current, token, item);
    } else {
        cJSON_AddItemToObject(current, token, item);
    }

    free(path_copy);
}

/**
 * @brief Simple auto-typing parser for command line values.
 */
static cJSON* parse_arg_value(const char* val) {
    if (strcmp(val, "true") == 0)
        return cJSON_CreateBool(true);
    if (strcmp(val, "false") == 0)
        return cJSON_CreateBool(false);

    // Check if it's a number (simplified)
    char* endptr = NULL;
    double d = strtod(val, &endptr);
    if (*endptr == '\0' && endptr != val) {
        return cJSON_CreateNumber(d);
    }

    // Otherwise, it's a string
    return cJSON_CreateString(val);
}

// ============================================================================
// Lifecycle
// ============================================================================

config* config_create(void) {
    return TO_CONFIG(cJSON_CreateObject());
}

void config_destroy(config** cfg) {
    if (cfg && *cfg) {
        cJSON_Delete(TO_JSON(*cfg));
        *cfg = NULL;
    }
}

int config_load_file(config* cfg, const char* path) {
    if (!cfg || !path)
        return -EINVAL;

    char* json_str = read_file_to_string(path);
    if (!json_str) {
        return -ENOENT;
    }

    cJSON* new_json = cJSON_Parse(json_str);
    free(json_str);

    if (!new_json) {
        return -EINVAL;  // Parse error
    }

    // Merge into existing config
    json_merge(TO_JSON(cfg), new_json);
    cJSON_Delete(new_json);  // Free the container, items were moved

    return 0;
}

int config_load_env(config* cfg) {
    if (!cfg)
        return -EINVAL;

    // Manual support for specific variables as per spec
    // SUNSPOTS_PORT -> port
    // SUNSPOTS_HOST -> host
    // SUNSPOTS_DEBUG -> debug

    const char* env_port = getenv("SUNSPOTS_PORT");
    if (env_port) {
        char* endptr = NULL;
        long val = strtol(env_port, &endptr, 10);
        if (*endptr == '\0') {
            cJSON* item = cJSON_CreateNumber((int) val);
            cJSON_ReplaceItemInObjectCaseSensitive(TO_JSON(cfg), "port", item);
        }
    }

    const char* env_host = getenv("SUNSPOTS_HOST");
    if (env_host) {
        cJSON* item = cJSON_CreateString(env_host);
        cJSON_ReplaceItemInObjectCaseSensitive(TO_JSON(cfg), "host", item);
    }

    const char* env_debug = getenv("SUNSPOTS_DEBUG");
    if (env_debug) {
        bool dbg = (strcmp(env_debug, "true") == 0 || strcmp(env_debug, "1") == 0);
        cJSON* item = cJSON_CreateBool(dbg);
        cJSON_ReplaceItemInObjectCaseSensitive(TO_JSON(cfg), "debug", item);
    }

    return 0;
}

int config_load_args(config* cfg, int argc, char** argv) {
    if (!cfg || argc < 1 || !argv)
        return -EINVAL;

    cJSON* overrides = cJSON_CreateObject();
    if (!overrides)
        return -ENOMEM;

    for (int i = 1; i < argc; i++) {
        // Look for --prefix
        if (strncmp(argv[i], "--", 2) == 0 && strlen(argv[i]) > 2) {
            const char* key = argv[i] + 2;

            // Expect a value in the next argument
            if (i + 1 < argc) {
                const char* val_str = argv[++i];
                cJSON* val_item = parse_arg_value(val_str);
                if (val_item) {
                    json_set_at_path(overrides, key, val_item);
                }
            } else {
                // Missing value for flag
                cJSON_Delete(overrides);
                return -EINVAL;
            }
        }
    }

    // Merge overrides into the main config
    json_merge(TO_JSON(cfg), overrides);
    cJSON_Delete(overrides);

    return 0;
}

// ============================================================================
// Navigation
// ============================================================================

const config* config_get_subtree(const config* root, const char* path) {
    cJSON* item = resolve_path(TO_CONST_JSON(root), path);
    return TO_CONST_CONFIG(item);
}

// ============================================================================
// Accessors (Defaults / Safe)
// ============================================================================

int config_get_int_or(const config* cfg, const char* key, int default_val) {
    if (!cfg || !key)
        return default_val;

    cJSON* item = resolve_path(TO_CONST_JSON(cfg), key);
    if (!item || !cJSON_IsNumber(item))
        return default_val;

    return item->valueint;
}

bool config_get_bool_or(const config* cfg, const char* key, bool default_val) {
    if (!cfg || !key)
        return default_val;

    cJSON* item = resolve_path(TO_CONST_JSON(cfg), key);
    if (!item)
        return default_val;

    if (cJSON_IsBool(item))
        return cJSON_IsTrue(item);

    // Fallback: handle strings "true"/"false" if loose parsing desired?
    // Standards say: Explicit over Implicit. So Strict JSON bool.

    return default_val;
}

const char* config_get_string_or(const config* cfg, const char* key, const char* default_val) {
    if (!cfg || !key)
        return default_val;

    cJSON* item = resolve_path(TO_CONST_JSON(cfg), key);
    if (!item || !cJSON_IsString(item))
        return default_val;

    return item->valuestring;
}

// ============================================================================
// Accessors (Strict)
// ============================================================================

int config_get_int(const config* cfg, const char* key, int* out) {
    if (!cfg || !key || !out)
        return -EINVAL;

    cJSON* item = resolve_path(TO_CONST_JSON(cfg), key);
    if (!item)
        return -ENOENT;
    if (!cJSON_IsNumber(item))
        return -EINVAL;  // mismatch type

    *out = item->valueint;
    return 0;
}

int config_get_bool(const config* cfg, const char* key, bool* out) {
    if (!cfg || !key || !out)
        return -EINVAL;

    cJSON* item = resolve_path(TO_CONST_JSON(cfg), key);
    if (!item)
        return -ENOENT;
    if (!cJSON_IsBool(item))
        return -EINVAL;

    *out = cJSON_IsTrue(item);
    return 0;
}

int config_get_string(const config* cfg, const char* key, char* out, size_t size) {
    if (!cfg || !key || !out || size == 0)
        return -EINVAL;

    cJSON* item = resolve_path(TO_CONST_JSON(cfg), key);
    if (!item)
        return -ENOENT;
    if (!cJSON_IsString(item))
        return -EINVAL;

    const char* src = item->valuestring;
    if (!src)
        return -EINVAL;  // Should not happen for String type

    size_t len = strlen(src);
    if (len >= size) {
        return -ENAMETOOLONG;
    }

    memcpy(out, src, len + 1);  // Safe because we checked length
    return 0;
}
