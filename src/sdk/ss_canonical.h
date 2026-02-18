#ifndef SS_CANONICAL_H
#define SS_CANONICAL_H

#include "sdk/ss_sdk_types.h"

/**
 * @brief Canonical metric identifiers generated from `ss_canonical.def`.
 */
typedef enum ss_metric_id {
#define X(id, canonical_name, value_type, unit) id,
#include "sdk/ss_canonical.def"
#undef X
    SS_METRIC_COUNT
} ss_metric_id;

/**
 * @brief Metadata for one canonical metric.
 */
typedef struct ss_metric_meta {
    /** Stable canonical dotted metric name. */
    const char *canonical_name;
    /** Canonical unit label. */
    const char *unit;
    /** Metric enum identifier. */
    ss_metric_id id;
    /** Canonical value type expected for metric records. */
    ss_sdk_value_type value_type;
} ss_metric_meta;

/**
 * @brief Get metadata for a canonical metric ID.
 *
 * @param id Metric identifier.
 * @return Pointer to metadata, or NULL when ID is invalid.
 */
const ss_metric_meta *ss_metric_meta_get(ss_metric_id id);

/**
 * @brief Get canonical metric name for a metric ID.
 *
 * @param id Metric identifier.
 * @return Canonical metric name or NULL when ID is invalid.
 */
const char *ss_metric_name(ss_metric_id id);

#endif
