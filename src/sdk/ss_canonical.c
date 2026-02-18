#include "sdk/ss_sdk.h"
#include "sdk/ss_canonical.h"

static const ss_metric_meta ss_metric_table[SS_METRIC_COUNT] = {
/* Designated initializers keep enum ID and metadata table index aligned. */
#define X(id, canonical_name, value_type, unit) [id] = { id, canonical_name, value_type, unit },
#include "sdk/ss_canonical.def"
#undef X
};

const ss_metric_meta *ss_metric_meta_get(ss_metric_id id)
{
    int idx = (int)id;

    if (idx < 0 || idx >= (int)SS_METRIC_COUNT) {
        return NULL;
    }

    return &ss_metric_table[idx];
}

const char *ss_metric_name(ss_metric_id id)
{
    const ss_metric_meta *metric_metadata = ss_metric_meta_get(id);
    if (metric_metadata == NULL) {
        return NULL;
    }
    return metric_metadata->canonical_name;
}
