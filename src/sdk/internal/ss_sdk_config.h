#ifndef SS_SDK_CONFIG_H
#define SS_SDK_CONFIG_H

#include <limits.h>
// ENV variables
#define SS_SDK_ENV_DB_DIR "SS_SDK_DB_DIR" // Env var key (string): absolute or relative DB directory for location-scoped DB files.
#define SS_SDK_ENV_LOG_LEVEL "SS_SDK_LOG_LEVEL" // Env var key (string enum): debug|info|warn|warning|error|off|none.
#define SS_SDK_ENV_LOG_MIRROR_ENABLED "SS_SDK_LOG_MIRROR_ENABLED" // Env var key (bool token): 1/true/yes/on enables, other values disable.
#define SS_SDK_ENV_LOG_MIRROR_PATH "SS_SDK_LOG_MIRROR_PATH" // Env var key (string): absolute or relative mirror log path.
#define SS_SDK_ENV_LOG_MIRROR_MAX_BYTES "SS_SDK_LOG_MIRROR_MAX_BYTES" // Env var key (uint): truncate mirror when size reaches/exceeds this many bytes.
#define SS_BACKFILL_ENV_RUN_ONCE "SUNSPOTS_BACKFILL_RUN_ONCE" // Env var key (bool token): bootstrap hint from backfill config `run_one`.

// Defaults if no ENV variables
#define SS_SDK_DB_DEFAULT_DIR "db" // Default DB directory (string path).
#define SS_SDK_LOG_MIRROR_DEFAULT_PATH "logs/sdk.log" // Default mirror path (string path).
#define SS_SDK_LOG_MIRROR_DEFAULT_MAX_BYTES (5U * 1024U * 1024U) // Default mirror file cap in bytes (5 MiB).
#define SS_SDK_LOG_LEVEL_DEFAULT "debug" // Default log level (string enum).
#define SS_SDK_LOG_MIRROR_ENABLED_DEFAULT 1 // Default mirror toggle (number/bool): 1 enabled, 0 disabled.

#if defined(PATH_MAX) && (PATH_MAX > 0)
#define SS_SDK_PATH_BUFFER_SIZE PATH_MAX // Compile-time path buffer size (number).
#else
#define SS_SDK_PATH_BUFFER_SIZE 1024 // Compile-time fallback path buffer size (number).
#endif

// Bootstrap SS_SDK_* env vars from SUNSPOTS_CONFIG JSON when daemon does not export SS_SDK_* directly.
// Precedence: existing SS_SDK_* env vars are preserved and never overridden.
void ss_sdk_config_bootstrap_env_from_blob(void);

#endif
