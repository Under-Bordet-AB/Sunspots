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
- `SS_SDK_CLAMPED`: call succeeded, and the bounded read was clamped to available data
- `SS_SDK_CLAMPED_PARTIAL_DATA`: bounded read was clamped, and completeness was still not met
- `SS_SDK_ERR_INVALID_ARG`: invalid input arguments
- `SS_SDK_ERR_VALIDATION`: input failed SDK validation
- `SS_SDK_ERR_PARTIAL_DATA`: data-completeness signal (result may be usable, but incomplete for the requested semantics)
- `SS_SDK_ERR_INTERNAL`: internal/config/I/O failure

`SS_SDK_CLAMPED` is not a failure. It means the caller asked for a bounded range that extended past the latest available stored slot, and the SDK returned the available bounded window instead.

`SS_SDK_CLAMPED_PARTIAL_DATA` means the SDK had to clamp the bounded request and, even after clamping, some requested slots still could not be satisfied.

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

### `ingested_utc` Behavior

`ss_sdk_record.ingested_utc` is public, but normal call sites do not need to set it manually.

Factory functions initialize it to `0`.

When a record is written with `ingested_utc == 0`:

1. Observation records keep `ingested_utc = 0`
2. Forecast records get a write-time ingest timestamp assigned by the SDK

This keeps existing module call sites backward-compatible while still letting forecast writes carry release identity.

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
if (st != SS_SDK_OK && st != SS_SDK_CLAMPED && st != SS_SDK_CLAMPED_PARTIAL_DATA) {
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
- If `quarters_to_fetch > 0` and the requested end extends past the latest available stored slot for that canonical, the read is clamped.
- If the clamped bounded read is otherwise complete, the SDK returns `SS_SDK_CLAMPED`.
- If the clamped bounded read is still incomplete, the SDK returns `SS_SDK_CLAMPED_PARTIAL_DATA`.

### Interpolation Behavior

Interpolation is applied by SDK read selection (`ss_sdk_db_get_canonical`) when an exact slot value is missing.

Slot selection order:

1. For `ts <= now_slot`: prefer observation, then forecast, then interpolation (if policy allows).
2. For `ts > now_slot`: prefer forecast first. For step-policy canonicals, observation can also be used as a direct fallback.

Interpolation policies are fixed per canonical metric:

1. `linear`: most continuous weather/FX metrics (for example temperature, humidity, wind, cloud cover, radiation, FX).
2. `step`: spot price metrics (`energy.price.spot.*`).
3. `none`: discrete metrics (for example symbol code, boolean day/night).

Interpolation limits:

1. Max interpolation span is 6 hours.
2. If a required slot would need interpolation beyond that limit, read returns `SS_SDK_ERR_PARTIAL_DATA`.
3. If some slots are missing after selection/interpolation, read returns `SS_SDK_ERR_PARTIAL_DATA` and may still return partial samples.

Flags in returned samples:

1. `SS_SDK_SAMPLE_OBSERVED`
2. `SS_SDK_SAMPLE_FORECAST`
3. `SS_SDK_SAMPLE_INTERPOLATED`

Caller guidance:

1. Treat `SS_SDK_CLAMPED` as successful data with a bounded-window warning.
2. Treat `SS_SDK_CLAMPED_PARTIAL_DATA` and `SS_SDK_ERR_PARTIAL_DATA` as explicit completeness signals.
3. Always call `ss_sdk_db_free_samples(&out)` when `out.samples` is non-NULL, including partial-data cases.

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
- SDK log verbosity is controlled by `SS_SDK_LOG_LEVEL`:
  - `debug`, `info`, `warn`/`warning`, `error`, `off`/`none`
- Optional SDK file mirror is controlled by env vars:
  - `SS_SDK_LOG_MIRROR_ENABLED=1` (accepted truthy tokens: `1`, `true`, `yes`, `on`)
  - `SS_SDK_LOG_MIRROR_PATH=/path/to/sdk.log`
  - `SS_SDK_LOG_MIRROR_MAX_BYTES=5242880` (truncate mirror when file size reaches/exceeds this cap)
- DB directory is controlled by `SS_SDK_DB_DIR`.
- Config convention for daemon/module config blobs:
  - `system.sdk.db_dir`
  - `system.sdk.log_level`
  - `system.sdk.log_mirror_enabled`
  - `system.sdk.log_mirror_path`
  - `system.sdk.log_mirror_max_bytes`
- Location identity and metadata come from `system.location`:
  - `system.location.id`
  - `system.location.latitude`
  - `system.location.longitude`
  - `system.location.name`
  - `system.location.elprisomrade`
- Default behavior with no SDK config/env:
  - DB path defaults to `db/<location_id>.db` (requires `SUNSPOTS_SYSTEM.location.id`)
  - Log level defaults to `debug`
  - Mirror defaults to on
  - Default mirror path is `logs/sdk.log` (if no path is provided)
  - Mirror max file size defaults to `5242880` bytes (5 MiB)
- SDK cleanup is automatic at process exit through internal `atexit` handling. Normal module code does not need to call an SDK shutdown API.
- With SDK mirror enabled, mirror writes are blocking per call: each log call locks the mirror file, writes, then unlocks.
- Performance guideline: avoid logging inside tight loops; collect state in loop variables and emit one summary log at the end (for example via a final `switch`/result branch).
- Recommended pattern for larger modules: use enum + lookup table for stable event strings.

### Team Config Handoff (Copy/Paste)

When teammates ask for the "SDK/DB/log strings", use one shared `system` block:

```json
"system": {
  "location": {
    "id": "stockholm-home",
    "name": "Stockholm",
    "latitude": 59.3293,
    "longitude": 18.0686,
    "elprisomrade": "SE3"
  },
  "sdk": {
    "db_dir": "db",
    "log_level": "info",
    "log_mirror_enabled": true,
    "log_mirror_path": "logs/sdk.log",
    "log_mirror_max_bytes": "5242880"
  }
}
```

Notes:

1. Keep exactly one shared SDK config under `system.sdk`.
2. Keep exactly one shared location identity under `system.location`, especially `location.id`.
3. Do not place SDK keys anywhere else in config.
4. `log_mirror_max_bytes` is a hard cap trigger: when the mirror file reaches/exceeds the cap, SDK truncates it before writing the next line.
5. No fallback locations are supported; only `system.sdk` and `system.location` are read.

## See Also

- `docs/manual/sdk_db_callsite_example.c`
- `docs/manual/sdk_log_callsite_example.c`
- `docs/designs/SDK_public_db_API.md`
