#include <stdio.h>
#include <time.h>

#include "sdk/ss_sdk.h"

/*
 * Documentation-only call-site examples.
 * Checks SDK call results, but keeps non-SDK input checks minimal.
 * Partial-data handling is intentionally omitted: treat any non-OK status
 * as failure in this example.
 */

static ss_sdk_status calculate_average_temperature_next_2h(double *out_avg_c)
{
    ss_sdk_samples_out out = {0};
    ss_sdk_status status;
    double sum = 0.0;
    size_t n = 0;

    /* Read 8 quarters (2 hours) from current slot (now). */
    status = ss_sdk_db_get_canonical(0, 8, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out);
    
    if (status != SS_SDK_OK) {
        return status;
    }

    for (size_t i = 0; i < out.count; ++i) {
        /* Temperature metric is f64 in canonical metadata. */
        if (out.samples[i].value_type != SS_SDK_VALUE_F64) {
            continue;
        }
        sum += out.samples[i].value.f64;
        n += 1;
    }

    if (n == 0 || out.count == 0) {
        ss_sdk_db_free_samples(&out);
        return SS_SDK_ERR_INTERNAL;
    }

    *out_avg_c = sum / (double)n;
    ss_sdk_db_free_samples(&out);
    return status;
}

static ss_sdk_status write_one_temperature_observation(double value_c)
{
    ss_sdk_record record;
    ss_sdk_status status;

    /* Build canonical record message (normalizes to 15-minute slot). */
    status = ss_sdk_record_make_f64(
        &record,
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        value_c,
        (int64_t)time(NULL), // Prefer to use timestamp from response JSON
        SS_SDK_DATA_OBSERVATION);
    if (status != SS_SDK_OK) {
        return status;
    }

    /* Persist one record. */
    return ss_sdk_db_write_record(&record);
}

void sdk_db_callsite_example(void)
{
    double avg_c = 0.0;
    ss_sdk_status read_st = calculate_average_temperature_next_2h(&avg_c);
    if (read_st != SS_SDK_OK) {
        printf("read failed: %d\n", (int)read_st);
        return;
    }

    ss_sdk_status write_st = write_one_temperature_observation(avg_c);
    if (write_st != SS_SDK_OK) {
        printf("write failed: %d\n", (int)write_st);
        return;
    }

    printf("wrote average temperature: %.2f C\n", avg_c);
}
