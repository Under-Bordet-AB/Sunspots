#ifndef SUNSPOTS_BACKFILL_PAYLOAD_H
#define SUNSPOTS_BACKFILL_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#include "backfill_common.h"

int backfill_build_archive_url(char out_url[BACKFILL_MAX_URL_LEN], const backfill_config *cfg, int64_t from_utc, int64_t to_utc);
int backfill_build_single_run_url(char out_url[BACKFILL_MAX_URL_LEN], const backfill_config *cfg, int64_t run_utc);
int backfill_write_archive_payload(const char *json_text, int64_t chunk_start_utc, int64_t chunk_end_utc, size_t *out_writes);
int backfill_write_single_run_payload(const char *json_text, int64_t run_utc, size_t *out_writes);

#endif
