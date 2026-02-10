#ifndef SS_SDK_TYPES_H
#define SS_SDK_TYPES_H

/**
 * @file ss_sdk_types.h
 * @brief Shared SDK type definitions used across public headers.
 *
 * This header exists to avoid circular includes between `ss_sdk.h` and
 * `ss_canonical.h`.
 */

/**
 * @brief Canonical SDK value kinds.
 */
typedef enum {
    SS_SDK_VALUE_I64 = 0,
    SS_SDK_VALUE_F64,
    SS_SDK_VALUE_STR,
    SS_SDK_VALUE_BOOL
} ss_sdk_value_type;

#endif

