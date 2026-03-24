#ifndef TRANSFORM_H
#define TRANSFORM_H

/**
 * @brief Status codes returned by transformation functions.
 */
typedef enum {
    TRANSFORM_OK = 0,            /**< Operation successful */
    TRANSFORM_INVALID_INPUT,     /**< Input is NULL or invalid type */
    TRANSFORM_MISSING_FIELD,     /**< Required JSON field is missing */
    TRANSFORM_BAD_FORMAT,        /**< Data format is incorrect */
    TRANSFORM_UNSUPPORTED,       /**< Unsupported unit or format */
} transform_status_t;

#endif