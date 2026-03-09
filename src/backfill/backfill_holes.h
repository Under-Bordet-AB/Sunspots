#ifndef SUNSPOTS_BACKFILL_HOLES_H
#define SUNSPOTS_BACKFILL_HOLES_H

#include <stdint.h>
#include <stddef.h>

#include "../sdk/ss_sdk.h"
#include "backfill_common.h"

int backfill_hole_list_push_merge(hole_list *holes, int64_t from_utc, int64_t to_utc);
void backfill_hole_list_free(hole_list *holes);
int backfill_detect_all_holes(int64_t start_utc, int64_t end_utc, hole_list *holes);
size_t backfill_count_total_chunks(const hole_list *holes, int chunk_days);

#endif
