# SDK Manual

This manual is for SDK users. It focuses on call-site usage and expected behavior.
It does not cover internal implementation details.

## Includes

```c
#include "sdk/ss_sdk.h"
```

## Status Codes

Most SDK calls return `ss_sdk_status`.

- `SS_SDK_OK`: call succeeded
- `SS_SDK_ERR_INVALID_ARG`: invalid input arguments
- `SS_SDK_ERR_VALIDATION`: input failed SDK validation
- `SS_SDK_ERR_PARTIAL_DATA`: data-completeness signal (result may be usable, but incomplete for the requested semantics)
- `SS_SDK_ERR_INTERNAL`: internal/config/I/O failure

`SS_SDK_ERR_PARTIAL_DATA` is not always a hard failure in product terms. Treat it as "you should know data is incomplete" and handle according to your module policy.

## Database Usage

### Write One Canonical Record

```c
ss_sdk_record rec;
ss_sdk_status st;
// Create record with SDK factory funciton
st = ss_sdk_record_make_f64(
    &rec,
    SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
    7.5,
    (int64_t)time(NULL),
    SS_SDK_DATA_OBSERVATION
);
if (st != SS_SDK_OK) {
    return st;
}
// Write record to DB
st = ss_sdk_db_write_record(&rec);
if (st != SS_SDK_OK) {
    return st;
}
```

### Type-Safe Factory Rule

Use the record factory that matches the metric value type:

- `ss_sdk_record_make_f64` for `SS_SDK_VALUE_F64` metrics (for example, temperature).
- `ss_sdk_record_make_i64` for `SS_SDK_VALUE_I64` metrics.
- `ss_sdk_record_make_bool` for `SS_SDK_VALUE_BOOL` metrics.

If you use the wrong factory (wrong function), the SDK returns `SS_SDK_ERR_VALIDATION`.

### Read Canonical Samples

Prefer named query variables instead of literals (`0`, `8`, inline metric ids), especially when reading many canonical series.

```c
ss_sdk_samples_out out = {0};
ss_sdk_status st;
double sum = 0.0;
size_t n = 0;
const int64_t from_utc = 0; /* 0 => start from and with current 15-minute slot */
const uint16_t quarters_to_fetch = 0; /* 0 => forward horizon */

st = ss_sdk_db_get_canonical(from_utc, quarters_to_fetch, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out);
if (st != SS_SDK_OK) {
    return st;
}

for (size_t i = 0; i < out.count; ++i) {
    if (out.samples[i].value_type != SS_SDK_VALUE_F64) {
        continue;
    }
    sum += out.samples[i].value.f64;
    n += 1;
}

ss_sdk_db_free_samples(&out);
if (n == 0) {
    return SS_SDK_ERR_INTERNAL;
}

double avg_c = sum / (double)n;
```

### Read Behavior Notes

- `from_utc == 0` starts from current 15-minute slot.
- `from_utc > 0` is floor-aligned to 15-minute slot.
- `quarters_to_fetch > 0` reads exactly that many 15-minute slots.
- `quarters_to_fetch == 0` reads forward horizon (from start to latest available data).

## Logging Usage

### Log Macros (One-Liner)

Available helper macros:

- `SS_LOG_DEBUG(event, message)`
- `SS_LOG_INFO(event, message)`
- `SS_LOG_WARN(event, message)`
- `SS_LOG_ERROR(event, message)`

`event` and `message` are plain strings.  
`event` is optional (`NULL` allowed).

```c
ss_sdk_status st = SS_LOG_INFO(NULL, "starting temperature pipeline");
if (st != SS_SDK_OK) {
    return st;
}
```

### Structured Logging

```c
ss_sdk_log_fields fields = {
    .module = "compute.average_temperature",
    .source_api = "openmeteo",
    .metric = SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
    .ts_utc = (int64_t)time(NULL)
};

ss_sdk_status st = ss_sdk_log_write_fields(
    SS_SDK_LOG_WARN,
    "compute.input.partial",
    "using fallback interpolation for one slot",
    &fields,
    __FILE__,
    __LINE__,
    __func__
);
if (st != SS_SDK_OK) {
    return st;
}
```

### Logging Notes

- Macros auto-capture file/line/function.
- All log APIs return `ss_sdk_status`.
- SDK logs are written to the process logger (`syslog`/journald).
- Extra SDK debug logs are enabled by default (`SS_SDK_DEBUG` unset).
- Set `SS_SDK_DEBUG=0` to disable extra SDK debug logs.
- Optional SDK file mirror is controlled by env vars:
  - `SS_SDK_LOG_MIRROR_ENABLED=1`
  - `SS_SDK_LOG_MIRROR_PATH=/path/to/sdk.log`
- Default behavior with no SDK config/env:
  - DB path defaults to `db/ss_sdk.db`
  - Debug logging defaults to on
  - Mirror defaults to on
  - Default mirror path is `logs/sdk.log` (if no path is provided)
- Recommended pattern for larger modules: use enum + lookup table for stable event strings.

## See Also

- `docs/manual/sdk_db_callsite_example.c`
- `docs/manual/sdk_log_callsite_example.c`
- `docs/designs/SDK_public_db_API.md`
