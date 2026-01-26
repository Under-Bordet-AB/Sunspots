#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @file config.h
 * @brief Configuration module.
 *
 * DESIGN PATTERN: Subtree-Based Access
 * - The "Root" config object owns all memory.
 * - Subsections are "Subtrees" (borrowed pointers) into the tree.
 * - Users only destroy the Root.
 * - All accessors accept NULL handles (returning defaults).
 */

// Opaque handle to a configuration node
typedef struct config config;

// ============================================================================
// Lifecycle (Root Only)
// ============================================================================

/**
 * @brief Create an empty configuration root name.
 */
config* config_create(void);

/**
 * @brief Destroy the root configuration and ALL children.
 *        Pointers obtained via config_get_section() become invalid.
 */
void config_destroy(config** cfg);

/**
 * @brief Load and merge a JSON file into the config.
 * @return 0 on success, -ENOENT if file missing.
 */
int config_load_file(config* cfg, const char* path);

/**
 * @brief Load environment variables (SUNSPOTS_*). Environment variables are
 *        not discovered automatically, they must be implemented manually.
 */
int config_load_env(config* cfg);

/**
 * @brief Load command line arguments (--key value).
 *        Supports dot-notation for nested keys (e.g. --server.port 8080).
 */
int config_load_args(config* cfg, int argc, char** argv);

// ============================================================================
// Navigation (Subtrees)
// ============================================================================

/**
 * @brief Get a standard "Subtree" of the configuration.
 *
 * @param root Parent config object.
 * @param path Dot-notation path (e.g. "modules.server").
 * @return Pointer to the subtree, or NULL if not found.
 *         WARNING: Do NOT free this pointer. It is owned by root.
 */
const config* config_get_subtree(const config* root, const char* path);

// ============================================================================
// Accessors (Defaults / Safe)
// ============================================================================

/**
 * @brief Get an integer with a default fallback.
 *        Safe to pass NULL cfg.
 */
int config_get_int_or(const config* cfg, const char* key, int default_val);

/**
 * @brief Get a boolean with a default fallback.
 *        Safe to pass NULL cfg.
 */
bool config_get_bool_or(const config* cfg, const char* key, bool default_val);

/**
 * @brief Get a string with a default fallback.
 *        Returns a pointer to internal memory (valid until root destroyed).
 *        Safe to pass NULL cfg.
 */
const char* config_get_string_or(const config* cfg, const char* key, const char* default_val);

// ============================================================================
// Accessors (Strict)
// ============================================================================

/**
 * @brief Strict retrieval. Returns error if missing.
 * @return 0 on success, -ENOENT if missing.
 */
int config_get_int(const config* cfg, const char* key, int* out);

int config_get_bool(const config* cfg, const char* key, bool* out);

// Retrieve string into a user-provided buffer
int config_get_string(const config* cfg, const char* key, char* out, size_t size);

#endif  // CONFIG_H
