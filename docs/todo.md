# Sunspots TODO - File-Driven Process Architecture

## Non-negotiable architecture constraints

- The daemon is only a supervisor: start workers, monitor heartbeats, restart on failure.
- No business/data IPC between workers (no sockets/shared memory message passing for core flow).
- Inter-process communication for business data is file-based only.
- Shared data exchange uses atomic append/read with file locks (via `atomic_file_rw`).
- Workers are decoupled: they do not call each other directly, they communicate through files.

## Pipeline model (conveyor belt)

1. Fetchers read external APIs.
2. Fetchers normalize data.
3. Fetchers append normalized records to the shared DB file.
4. Calculators read records and produce computed outputs (energy plan, etc.).
5. Calculators write endpoint JSON artifacts under `endpoints/`.
6. Server dynamically serves endpoint artifacts as API responses.

## Extensibility contract

- New calculator = new process + daemon supervision + reads DB + writes endpoint JSON.
- No changes required in other workers, as long as schema/paths are respected.
- Server should expose new endpoint artifacts dynamically.

## SDK direction (to speed up fetchers/calculators)

### Why

- Fetchers and calculators share the same lifecycle shape: read config -> read input -> normalize/compute -> write canonical records/artifacts -> heartbeat.
- Repeating this manually in each process will create boilerplate and drift.
- The server should stay thin; shared logic should live in an SDK used by workers.

### SDK scope (worker-focused)

- `worker_sdk` should provide:
  - process bootstrap (env/config loading),
  - heartbeat loop integration,
  - safe DB read/append wrappers,
  - schema validation,
  - endpoint artifact writer (atomic temp + rename),
  - logging helpers and metrics counters.

### Canonical data model (must be centralized)

- Add one registry of canonical metric/value types (example: `temperature_c`, `wind_speed_ms`, `spot_price_sek_kwh`, `solar_irradiance_wm2`).
- Fetchers manually map provider-specific fields to canonical types (this is the only required custom mapping step).
- SDK enforces allowed canonical types at write time.

### Plugin-style module contract

- Fetcher plugin contract:
  - `fetch_raw()`
  - `normalize_to_canonical()`
  - `emit_records()`
- Calculator plugin contract:
  - `read_inputs()`
  - `compute_outputs()`
  - `emit_endpoint_artifacts()`
- Daemon treats both as generic workers; only binary + heartbeat config differ.

## SDK tutorial sketch (design first, implement second)

Goal: teach usage first. This is a usage-oriented sketch, not a final header.

### 1) How a worker would include and bootstrap

```c
// fetch_openmeteo.c
#include "worker_sdk.h"

int main(int argc, char** argv) {
    wsdk_runtime rt = {0};
    if (wsdk_bootstrap(&rt, argc, argv) != 0) {
        return 1;
    }

    while (wsdk_should_run(&rt)) {
        wsdk_heartbeat(&rt);
        // fetch -> normalize -> emit canonical records
        wsdk_sleep_interval(&rt);
    }

    wsdk_shutdown(&rt);
    return 0;
}
```

### 2) How a fetcher writes canonical records

```c
// inside fetch loop
wsdk_record rec = {0};
rec.source = "openmeteo";
rec.type = WSDK_TYPE_TEMPERATURE_C;
rec.timestamp_unix = now_ts;
rec.payload_json = "{\"value\": 14.2, \"lat\": 59.3, \"lon\": 18.0}";

if (wsdk_emit_record(&rt, &rec) != 0) {
    wsdk_log_error(&rt, "emit failed");
}
```

### 3) How a calculator reads and publishes endpoint artifacts

```c
wsdk_reader rd = {0};
wsdk_reader_open(&rt, &rd, WSDK_READ_SINCE_LAST_OFFSET);

while (wsdk_reader_next(&rd, &rec) == 1) {
    // aggregate / compute energy plan
}

const char* endpoint_json = "{ \"plan\": [...] }";
wsdk_publish_endpoint(&rt, "energy_plan", endpoint_json); // atomic temp+rename
wsdk_reader_close(&rd);
```

### 4) Sketch of header surface (small first version)

```c
// worker_sdk.h (sketch, not final)
typedef struct wsdk_runtime wsdk_runtime;
typedef struct wsdk_reader wsdk_reader;

typedef enum {
    WSDK_TYPE_TEMPERATURE_C,
    WSDK_TYPE_WIND_SPEED_MS,
    WSDK_TYPE_SPOT_PRICE_SEK_KWH,
    WSDK_TYPE_SOLAR_IRRADIANCE_WM2
} wsdk_type;

typedef struct {
    const char* source;
    wsdk_type type;
    long timestamp_unix;
    const char* payload_json; // validated JSON object/string by SDK
} wsdk_record;

int wsdk_bootstrap(wsdk_runtime* rt, int argc, char** argv);
int wsdk_should_run(wsdk_runtime* rt);
int wsdk_heartbeat(wsdk_runtime* rt);
int wsdk_sleep_interval(wsdk_runtime* rt);
int wsdk_shutdown(wsdk_runtime* rt);

int wsdk_emit_record(wsdk_runtime* rt, const wsdk_record* rec);

int wsdk_reader_open(wsdk_runtime* rt, wsdk_reader* rd, int mode);
int wsdk_reader_next(wsdk_reader* rd, wsdk_record* out);
int wsdk_reader_close(wsdk_reader* rd);

int wsdk_publish_endpoint(wsdk_runtime* rt, const char* endpoint_name, const char* json_body);
int wsdk_log_error(wsdk_runtime* rt, const char* msg);
```

### 5) What stays manual per fetcher/calculator

- Fetcher-specific API call logic.
- Field mapping from provider schema -> canonical `wsdk_type`.
- Calculator-specific computation logic.

Everything else should be SDK boilerplate.

## Immediate implementation tasks (P0)

- Replace hardcoded watcher path in `src/core/fetch_data.c` with real DB path (`.db/database.jsonl`) from config/env.
- Wire config from supervisor to workers using environment variables (replace temporary `TEST` env in `src/core/main.c`).
- Define and enforce one record schema for DB writes.
- Implement parsing + validation in worker(s) before compute/store actions.
- Ensure each worker uses clear read/write file contracts (input file, output file, schema version).
- Extract shared worker code into first SDK module (`worker_sdk`) before adding more fetchers/calculators.

## Data contract (must define now)

- DB format: NDJSON (one event per line).
- Required envelope fields: `version`, `source`, `type`, `timestamp`, `payload`.
- Backward compatibility rule for schema upgrades.
- Error handling rule for bad lines (skip/log/quarantine).

## Runtime reliability tasks (P1)

- Add fallback polling (mtime/size check) alongside inotify to reduce missed update risk.
- Add DB rotation/compaction strategy to control file growth.
- Add endpoint artifact write strategy (temp file + rename) for atomic publishes.
- Add health metrics per worker (records read/written, parse errors, compute latency).

## Build and integration tasks (P1)

- Split build targets so daemon/workers/tests are separate binaries (avoid multi-`main` link conflicts).
- Add integration test that verifies end-to-end flow:
  - append input record -> compute process runs -> endpoint JSON updated.
- Add contract tests for schema validation.

## Done criteria

- Daemon only supervises (no business logic/data transforms).
- At least one full pipeline runs end-to-end using only file contracts.
- A new calculator can be added without modifying existing worker internals.
- Server successfully serves computed endpoint artifacts from `endpoints/`.
