#include "backfill_holes.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    BACKFILL_HOLE_SCAN_SLOTS = 672
};

typedef struct {
    int64_t win_start;
    int64_t win_end;
    uint16_t quarters;
    const ss_sdk_samples_out *samples;
    bool **out_present;
} present_mark_ctx;

int backfill_hole_list_push_merge(hole_list *holes, int64_t from_utc, int64_t to_utc)
{
    hole_range *grown;

    if (holes == NULL || from_utc >= to_utc) {
        return 0;
    }

    if (holes->count > 0U) {
        hole_range *last = &holes->items[holes->count - 1U];
        if (from_utc <= last->to_utc) {
            if (to_utc > last->to_utc) {
                last->to_utc = to_utc;
            }
            return 1;
        }
    }

    if (holes->count == holes->cap) {
        size_t new_cap = holes->cap == 0U ? 16U : holes->cap * 2U;
        grown = (hole_range *)realloc(holes->items, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return 0;
        }
        holes->items = grown;
        holes->cap = new_cap;
    }

    holes->items[holes->count].from_utc = from_utc;
    holes->items[holes->count].to_utc = to_utc;
    holes->count += 1U;
    return 1;
}

void backfill_hole_list_free(hole_list *holes)
{
    if (holes == NULL) {
        return;
    }
    free(holes->items);
    holes->items = NULL;
    holes->count = 0U;
    holes->cap = 0U;
}

static int detect_holes_window_end(int64_t win_start, int64_t end_utc, int64_t *out_end, uint16_t *out_quarters)
{
    int64_t win_end;
    int64_t span_slots;

    if (out_end == NULL || out_quarters == NULL) {
        return -1;
    }

    win_end = win_start + ((int64_t)BACKFILL_HOLE_SCAN_SLOTS * BACKFILL_SLOT_SECONDS);
    if (win_end > end_utc) {
        win_end = end_utc;
    }

    span_slots = (win_end - win_start) / BACKFILL_SLOT_SECONDS;
    if (span_slots <= 0 || span_slots > UINT16_MAX) {
        return -1;
    }

    *out_end = win_end;
    *out_quarters = (uint16_t)span_slots;
    return 0;
}

static int mark_present_slots(const present_mark_ctx *ctx)
{
    bool *present = NULL;
    size_t i;

    if (ctx == NULL || ctx->samples == NULL || ctx->out_present == NULL) {
        return -1;
    }

    present = (bool *)calloc((size_t)ctx->quarters, sizeof(bool));
    if (present == NULL) {
        return -1;
    }

    for (i = 0U; i < ctx->samples->count; ++i) {
        int64_t ts = ctx->samples->samples[i].ts_utc;
        if (ts >= ctx->win_start && ts < ctx->win_end) {
            size_t idx = (size_t)((ts - ctx->win_start) / BACKFILL_SLOT_SECONDS);
            if (idx < (size_t)ctx->quarters) {
                present[idx] = true;
            }
        }
    }

    *ctx->out_present = present;
    return 0;
}

static int push_missing_ranges(int64_t win_start, uint16_t quarters, const bool *present, hole_list *holes)
{
    size_t i = 0U;

    if (present == NULL || holes == NULL) {
        return -1;
    }

    while (i < (size_t)quarters) {
        if (!present[i]) {
            size_t miss_start = i;
            while (i < (size_t)quarters && !present[i]) {
                i += 1U;
            }
            if (!backfill_hole_list_push_merge(
                    holes,
                    win_start + ((int64_t)miss_start * BACKFILL_SLOT_SECONDS),
                    win_start + ((int64_t)i * BACKFILL_SLOT_SECONDS))) {
                return -1;
            }
        } else {
            i += 1U;
        }
    }

    return 0;
}

static int detect_holes_for_metric(ss_metric_id metric, int64_t start_utc, int64_t end_utc, hole_list *holes)
{
    int64_t win_start = start_utc;

    while (win_start < end_utc) {
        int64_t win_end = 0;
        uint16_t quarters;
        ss_sdk_samples_out out = {0};
        bool *present = NULL;
        present_mark_ctx mark_ctx;
        ss_sdk_status st;

        if (detect_holes_window_end(win_start, end_utc, &win_end, &quarters) != 0) {
            break;
        }

        st = ss_sdk_db_get_canonical(win_start, quarters, metric, &out);
        if (st != SS_SDK_OK &&
            st != SS_SDK_CLAMPED &&
            st != SS_SDK_CLAMPED_PARTIAL_DATA &&
            st != SS_SDK_ERR_PARTIAL_DATA) {
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        mark_ctx.win_start = win_start;
        mark_ctx.win_end = win_end;
        mark_ctx.quarters = quarters;
        mark_ctx.samples = &out;
        mark_ctx.out_present = &present;
        if (mark_present_slots(&mark_ctx) != 0) {
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        if (push_missing_ranges(win_start, quarters, present, holes) != 0) {
            free(present);
            ss_sdk_db_free_samples(&out);
            return -1;
        }

        free(present);
        ss_sdk_db_free_samples(&out);
        win_start = win_end;
    }

    return 0;
}

int backfill_detect_all_holes(int64_t start_utc, int64_t end_utc, hole_list *holes)
{
    if (holes == NULL || start_utc >= end_utc) {
        return -1;
    }
    if (detect_holes_for_metric(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, start_utc, end_utc, holes) != 0) {
        return -1;
    }
    if (detect_holes_for_metric(SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, start_utc, end_utc, holes) != 0) {
        return -1;
    }
    if (detect_holes_for_metric(SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2, start_utc, end_utc, holes) != 0) {
        return -1;
    }
    return 0;
}

size_t backfill_count_total_chunks(const hole_list *holes, int chunk_days)
{
    size_t total = 0U;
    size_t i;
    if (holes == NULL || chunk_days <= 0) {
        return 0U;
    }

    for (i = 0U; i < holes->count; ++i) {
        int64_t span = holes->items[i].to_utc - holes->items[i].from_utc;
        if (span > 0) {
            int64_t chunk_seconds = (int64_t)chunk_days * 86400;
            total += (size_t)((span + chunk_seconds - 1) / chunk_seconds);
        }
    }
    return total;
}
