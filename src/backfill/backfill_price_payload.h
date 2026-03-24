#ifndef SUNSPOTS_BACKFILL_PRICE_PAYLOAD_H
#define SUNSPOTS_BACKFILL_PRICE_PAYLOAD_H

#include "backfill_common.h"

int backfill_build_elprisjustnu_day_url(
    char out_url[BACKFILL_MAX_URL_LEN],
    const backfill_config *cfg,
    int year,
    int month,
    int day);

int backfill_write_elprisjustnu_payload(
    const char *json_text,
    int64_t delivery_day_utc,
    size_t *out_writes);

#endif
