# SDK Module Manual

| Attribute | Value |
| :--- | :--- |
| **Module** | `src/sdk` |
| **Status** | Implementation Ready |
| **Pattern** | Public Facade + Internal Modules |
| **Public Header** | `src/sdk/ss_sdk.h` |
| **Internal Modules** | `src/sdk/internal/db`, `src/sdk/internal/log` |

## Overview

The SDK is the only public integration surface for data write/read and logging.

If you are building or updating a fetcher, calculator, or server endpoint, your code should include only:

```c
#include "sdk/ss_sdk.h"
```

You should not call DB internals or log internals directly.

### Why this boundary exists

- It keeps module contracts stable.
- It prevents accidental coupling to storage/logging implementation details.
- It lets the team swap internals later (for example, file DB -> SQLite) without changing caller code.

## Runtime Configuration (Important)

The SDK is a library, but it reads a small amount of runtime configuration:

- Logging sink path comes from `system.log_path` inside the `SUNSPOTS_CONFIG` environment variable.
- DB file path can be overridden via `SS_SDK_DB_PATH` (optional).

This matches the project design where the daemon loads `config/sunspots.json` and exports it for all child processes.

## Quick Start

### 1. Write canonical data

```c
ss_sdk_record rec = {0};

rec.metric = SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C;
rec.value_type = SS_SDK_VALUE_F64;
rec.value.f64 = 7.2;

rec.ts_start_utc = ts_utc;
rec.ts_end_utc = ts_utc;

rec.data_kind = SS_SDK_DATA_OBSERVATION;
rec.source_api = "smhi.snow1g";
rec.source_field = "air_temperature";
rec.source_tz = "UTC";

/* forecast fields are optional for observations */
rec.model_id = "";
rec.model_run_utc = 0;
rec.issued_at_utc = 0;

ss_sdk_status rc = ss_sdk_db_write_record(&rec);
```

Notes:

- `metric` must be a known canonical metric (see Canonical Metrics below).
- `value_type` must match the metric's canonical value type (SDK validates this).
- For forecasts, `model_id`, `model_run_utc`, and `issued_at_utc` are required.

### 2. Read data by weeks window

```c
ss_sdk_record *rows = NULL;
size_t count = 0;

ss_sdk_status rc = ss_sdk_db_get_last_weeks(2, &rows, &count);
if (rc == SS_SDK_OK) {
    for (size_t i = 0; i < count; i++) {
        /* use rows[i] */
    }
    ss_sdk_db_free_records(rows);
}
```

Notes:

- Negative weeks is invalid (`SS_SDK_ERR_INVALID_ARG`).
- `weeks == 0` means "from now forward" (prognosis window only).
- The request is silently capped by `SS_SDK_DB_MAX_WEEKS`.

### 3. Log events

```c
SS_LOG_INFO("fetch.success", "provider data fetched and normalized");
SS_LOG_WARN("fetch.retry", "temporary upstream timeout");
SS_LOG_ERROR("fetch.failed", "provider unavailable");
```

Notes:

- Logging is enabled only when `system.log_path` is non-empty.
- Each call writes a single plain-text line with timestamp and source location.
- The `event` parameter is a plain `const char *` string key, so spelling matters. Treat event keys as part of the API contract.
- If you use the same event key a lot, prefer a `#define` constant (example: `#define SS_EVT_FETCH_SUCCESS "fetch.success"`) to avoid misspelling it.

## Canonical Metrics

Canonical metric IDs are SDK-owned and generated from:

- `src/sdk/ss_canonical.def`

The metric enum and metadata are exposed by SDK headers, not by DB internals.

### Add a new canonical metric

1. Add one entry to `src/sdk/ss_canonical.def`.
2. Recompile.
3. Use the new enum in fetcher mapping.

No DB API changes are required for adding metrics.

## Data Model

Main canonical record:

- `ss_sdk_record`

Important fields:

- `metric`: canonical metric ID (enum from SDK catalog)
- `value_type` + `value`: typed value payload
- `ts_start_utc`, `ts_end_utc`: UTC epoch seconds
- `data_kind`: observation or forecast
- `source_api`, `source_field`, `source_tz`: provenance
- `model_id`, `model_run_utc`, `issued_at_utc`: forecast lineage

### Forecast rule

Forecasts are stored as point records, not arrays.

That means if one valid hour has many forecast issuances over time, each issuance is a separate row.
This is required for forecast accuracy/convergence analysis.

## DB API Contract

### Write

- `ss_sdk_db_write_record(const ss_sdk_record *record)`

Behavior:

- Validates record shape and canonical type compatibility.
- Performs blocking file write in v1.
- Idempotent by logical identity:
  - `metric`
  - `ts_start_utc`
  - `data_kind`
  - `issued_at_utc`
  - `model_id`
  - `source_api`

### Read

- `ss_sdk_db_get_last_weeks(int weeks, ss_sdk_record **out_records, size_t *out_count)`

Behavior:

- Returns all metrics, not filtered by metric.
- `weeks == 0` means now-forward (prognosis window).
- Maximum window is capped silently by:
  - `#define SS_SDK_DB_MAX_WEEKS 8`
- Return order is deterministic:
  - `ts_start_utc ASC`
  - `issued_at_utc ASC`
  - `metric ASC`

Memory ownership:

- SDK allocates `out_records`.
- Caller must free with `ss_sdk_db_free_records(...)`.

### DB file location (v1)

The v1 DB backend is file-backed (blocking) with a simple, stable line format.

- Default path: `logs/ss_sdk_db.tsv`
- Override path: set `SS_SDK_DB_PATH` to a different file path

The public API is already shaped so internals can later move to SQLite without changing callers.

## Logging API Contract

Primary call-site API is macro-based:

- `SS_LOG_DEBUG(event, message)`
- `SS_LOG_INFO(event, message)`
- `SS_LOG_WARN(event, message)`
- `SS_LOG_ERROR(event, message)`

Macros inject:

- file (`__FILE__`)
- line (`__LINE__`)
- function (`__func__`)

### Event keys (spelling matters)

The `event` argument is a string, not an enum. This is intentional (easy to use, no extra coupling), but it means:

- event keys must be stable and consistently spelled across modules
- a typo creates a new event key and makes logs harder to grep/aggregate

Recommended convention is dotted names like `fetch.success` or `db.write_failed`.

Future improvement idea:

- define event keys as constants in a header (example: `#define SS_EVT_FETCH_SUCCESS "fetch.success"`) so call sites stop repeating raw strings
- optionally add a validator for known event keys in debug builds

### Log sink behavior

Configured from `system.log_path` in `config/sunspots.json`.

- If `system.log_path` is non-empty:
  - append plain-text log lines to that file
- If `system.log_path` is empty:
  - SDK file logging is disabled

Example line:

```text
2026-02-09T19:34:11Z WARN fetch.normalize_failed file=fetch_openmeteo.c line=74 func=normalize_data msg="unsupported unit in provider payload"
```

### Log file location (v1)

Log file writes are controlled by config:

- `SUNSPOTS_CONFIG` must contain `system.log_path`.
- If `system.log_path` is empty, SDK logging is disabled.
- If it is non-empty, SDK appends plain-text log lines to that path.

## Under the Hood (v1 internals)

### DB backend (`src/sdk/internal/db`)

- File-backed storage (single file)
- Blocking read/write
- File lock (`flock`) for cross-process safety
- String escaping for safe line serialization
- Read returns heap-allocated record array with deep-copied strings

This backend is intentionally simple and predictable for v1.
The public API is already shaped so internals can later move to SQLite without changing callers.

### Log backend (`src/sdk/internal/log`)

- Plain-text append file sink
- Reads `system.log_path` from `SUNSPOTS_CONFIG` payload
- Blocking append with lock
- Structured event fields flattened to readable single-line logs

## Error Handling and Troubleshooting

### Common return codes

- `SS_SDK_OK`: success
- `SS_SDK_ERR_INVALID_ARG`: null pointer, negative weeks, or invalid log inputs
- `SS_SDK_ERR_VALIDATION`: record fails canonical constraints (unknown metric, wrong type, invalid timestamps, missing forecast lineage)
- `SS_SDK_ERR_INTERNAL`: file/lock/IO failure

### Common issues

1) "No logs appear in my log file"

- Check that `SUNSPOTS_CONFIG` is set in that process environment.
- Check that `system.log_path` is non-empty in the JSON.
- Check filesystem permissions for the configured path (for example `/var/log/...` may require elevated permissions).

2) "DB read returns zero rows"

- Remember `weeks == 0` reads now-forward only (prognosis window).
- Ensure `SS_SDK_DB_PATH` matches the file being written (if you override it).

3) "Write returns validation error"

- Ensure `metric` is a known `SS_METRIC_*` enum.
- Ensure `record.value_type` matches the canonical type for that metric.
- Ensure timestamps are positive and `ts_end_utc >= ts_start_utc`.
- For forecasts, ensure `model_id`, `model_run_utc`, and `issued_at_utc` are set.

## API Reference

### Core Types

- `ss_sdk_status`
- `ss_metric_id`
- `ss_sdk_value_type`
- `ss_sdk_value`
- `ss_sdk_data_kind`
- `ss_sdk_log_level`
- `ss_sdk_record`
- `ss_sdk_log_fields`

### DB

- `ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record)`
- `ss_sdk_status ss_sdk_db_get_last_weeks(int weeks, ss_sdk_record **out_records, size_t *out_count)`
- `void ss_sdk_db_free_records(ss_sdk_record *records)`

### Logging

- `ss_sdk_status ss_sdk_log_write_auto(...)`
- `ss_sdk_status ss_sdk_log_write_fields(...)`
- `SS_LOG_DEBUG/INFO/WARN/ERROR` macros

### Lifecycle

- `void ss_sdk_shutdown(void)`

Use at process shutdown if you want explicit teardown point.

## Implementation Order (Top-Down)

1. Keep `src/sdk/ss_sdk.h` as the stable public contract.
2. Keep `src/sdk/ss_canonical.*` as SDK-owned metric source of truth.
3. Iterate internals in `src/sdk/internal/db` and `src/sdk/internal/log`.
4. Update fetchers to use SDK only.

This keeps architecture clean and easy for new developers to follow.
