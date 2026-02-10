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

#endif
