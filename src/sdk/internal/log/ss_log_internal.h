#ifndef SS_LOG_INTERNAL_H
#define SS_LOG_INTERNAL_H

#include "sdk/ss_sdk.h"

/**
 * @brief Internal logger call used by SDK macro wrapper.
 *
 * @param level Log severity.
 * @param event Stable event key.
 * @param message Human-readable text.
 * @param file Source file path.
 * @param line Source line number.
 * @param func Source function name.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_internal_log_write_auto(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const char *file,
    int line,
    const char *func
);

/**
 * @brief Internal structured logger call used by advanced SDK API.
 *
 * @param level Log severity.
 * @param event Stable event key.
 * @param message Human-readable text.
 * @param fields Optional structured metadata.
 * @param file Source file path.
 * @param line Source line number.
 * @param func Source function name.
 * @return SDK status code.
 */
ss_sdk_status ss_sdk_internal_log_write_fields(
    ss_sdk_log_level level,
    const char *event,
    const char *message,
    const ss_sdk_log_fields *fields,
    const char *file,
    int line,
    const char *func
);

/**
 * @brief Internal logger shutdown hook.
 */
void ss_sdk_internal_log_shutdown(void);

#ifdef SS_SDK_ENABLE_TEST_HOOKS
typedef enum {
    SS_SDK_LOG_HOOK_FAIL_STRDUP = 0,
    SS_SDK_LOG_HOOK_FAIL_MKDIR,
    SS_SDK_LOG_HOOK_FORCE_WRITE_ZERO,
    SS_SDK_LOG_HOOK_FORCE_WRITE_EINTR,
    SS_SDK_LOG_HOOK_FAIL_FSYNC,
    SS_SDK_LOG_HOOK_FAIL_FSYNC_CALL,
    SS_SDK_LOG_HOOK_FAIL_FLOCK,
    SS_SDK_LOG_HOOK_FAIL_GMTIME,
    SS_SDK_LOG_HOOK_FAIL_STRFTIME,
    SS_SDK_LOG_HOOK_FAIL_ESCAPE_BASE,
    SS_SDK_LOG_HOOK_FAIL_ESCAPE_OPTIONAL,
    SS_SDK_LOG_HOOK_FAIL_FORMAT_LINE,
    SS_SDK_LOG_HOOK_FAIL_CHECKED_ADD,
    SS_SDK_LOG_HOOK_FAIL_ESCAPE_ALLOC,
    SS_SDK_LOG_HOOK_FORCE_FORMAT_NEEDED_NEG,
    SS_SDK_LOG_HOOK_FORCE_FORMAT_ALLOC_NULL
} ss_sdk_log_test_hook;

void ss_sdk_internal_log_test_reset_hooks(void);
void ss_sdk_internal_log_test_set_hook(ss_sdk_log_test_hook hook, int count);

int ss_sdk_internal_log_test_checked_add_overflow(int *out_errno);
int ss_sdk_internal_log_test_ensure_parent_dirs(const char *path);
const char *ss_sdk_internal_log_test_level_to_string(ss_sdk_log_level level);
char *ss_sdk_internal_log_test_escape_text(const char *s);
char *ss_sdk_internal_log_test_strdup_local(const char *s);
int ss_sdk_internal_log_test_write_all(int fd, const char *buf, size_t len);
int ss_sdk_internal_log_test_extract_json_log_path(const char *json, char *out_path, size_t out_sz);
int ss_sdk_internal_log_test_get_log_path(char *out_path, size_t out_sz);
int ss_sdk_internal_log_test_consume_null_slot(void);
int ss_sdk_internal_log_test_format_utc_timestamp(char out_ts[32]);
void ss_sdk_internal_log_test_escaped_fields_free_null(void);
#endif

#endif
