#define _GNU_SOURCE

#include "backfill_common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int64_t backfill_align_to_slot(int64_t ts_utc)
{
    if (ts_utc < 0) {
        return 0;
    }
    return ts_utc - (ts_utc % BACKFILL_SLOT_SECONDS);
}

void backfill_copy_string_safe(char *dst, size_t dst_sz, const char *src)
{
    int n;

    if (dst == NULL || dst_sz == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    n = snprintf(dst, dst_sz, "%s", src);
    if (n < 0) {
        dst[0] = '\0';
        return;
    }
    if ((size_t)n >= dst_sz) {
        dst[dst_sz - 1U] = '\0';
    }
}

int backfill_parse_utc_hour(const char *s, int64_t *out_epoch)
{
    struct tm tmv;
    char *endp;
    time_t epoch;

    if (s == NULL || out_epoch == NULL) {
        return -1;
    }

    memset(&tmv, 0, sizeof(tmv));
    endp = strptime(s, "%Y-%m-%dT%H:%M", &tmv);
    if (endp == NULL || *endp != '\0') {
        return -1;
    }

    epoch = timegm(&tmv);
    if (epoch < 0) {
        return -1;
    }
    *out_epoch = (int64_t)epoch;
    return 0;
}

int backfill_parse_utc_date(const char *s, int64_t *out_epoch)
{
    struct tm tmv;
    char *endp;
    time_t epoch;

    if (s == NULL || out_epoch == NULL) {
        return -1;
    }

    memset(&tmv, 0, sizeof(tmv));
    endp = strptime(s, "%Y-%m-%d", &tmv);
    if (endp == NULL || *endp != '\0') {
        return -1;
    }
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;

    epoch = timegm(&tmv);
    if (epoch < 0) {
        return -1;
    }
    *out_epoch = (int64_t)epoch;
    return 0;
}

int backfill_epoch_to_ymd_utc(int64_t ts_utc, char out[11])
{
    time_t tv;
    struct tm tmv;

    if (out == NULL) {
        return -1;
    }

    tv = (time_t)ts_utc;
    if (gmtime_r(&tv, &tmv) == NULL) {
        return -1;
    }
    if (strftime(out, 11, "%Y-%m-%d", &tmv) != 10U) {
        return -1;
    }
    return 0;
}

int backfill_epoch_to_ymdhm_utc(int64_t ts_utc, char out[17])
{
    time_t tv;
    struct tm tmv;

    if (out == NULL) {
        return -1;
    }

    tv = (time_t)ts_utc;
    if (gmtime_r(&tv, &tmv) == NULL) {
        return -1;
    }
    if (strftime(out, 17, "%Y-%m-%dT%H:%M", &tmv) != 16U) {
        return -1;
    }
    return 0;
}
