# Sunspots Design Doc (Current Build Plan)

Last updated: 2026-02-05 (current runtime-aligned)

## Scope

This document is the active design for Sunspots during heavy development.

- We do **not** preserve legacy compatibility.
- Placeholder/sketch code is considered disposable.
- New implementation follows this document directly.

## Product Goal

Sunspots ingests weather + spot-price inputs, computes a time-based energy plan, and serves API-ready JSON artifacts.

Core flow:

`fetch -> normalize -> append DB -> compute -> publish endpoint artifact -> serve`

## Non-Negotiable Architecture Rules

- Daemon is supervisor-only: spawn, monitor heartbeat, restart workers.
- No business-data IPC between workers.
- Business data moves through files only.
- Shared writes are atomic and lock-safe.
- Workers are decoupled by schema + file contracts.
- Server reads endpoint artifacts only; it does not compute domain logic.

## Runtime Topology

Processes:

1. `sunspotsd` (daemon/supervisor)
2. `fetch_*` workers (provider adapters)
3. `calc_*` workers (domain compute)
4. `sunspots_server` (HTTP endpoint serving)

Each worker gets its own config slice and heartbeat settings from the daemon.

## Configuration Model

## Root precedence

`CLI args > env vars > config file`

## Distribution strategy

- Daemon loads one root config.
- Daemon validates root + worker requirements.
- Daemon writes per-worker slice files under `runtime/config/<worker>.json`.
- Daemon spawns process with:
  - `SUNSPOTS_CONFIG_PATH`
  - `SUNSPOTS_CONFIG_VERSION`

Workers never parse the root config and never read other workers' slices.

Worker polling config supports unit-based fields (`poll_interval_seconds`, `poll_interval_minutes`,
`poll_interval_hours`) so intervals do not need to be authored in milliseconds.

## Worker slice shape

```json
{
  "version": "string",
  "common": {
    "location": {
      "name": "Stockholm, Sweden",
      "latitude": "59.3293",
      "longitude": "18.0686"
    },
    "paths": {
      "db_file": ".db/database.jsonl",
      "endpoints_dir": "endpoints",
      "log_file": "logs/sunspots.jsonl"
    },
    "heartbeat": { "interval_ms": 1000 },
    "schema": { "version": 1 }
  },
  "worker": {}
}
```

## Data Contracts

## Shared DB format

- File type: NDJSON
- One event per line
- Required envelope keys:
  - `version`
  - `source`
  - `type`
  - `timestamp`
  - `payload`

Bad line policy: `skip + log + increment error metric`.

## Canonical metric registry

Canonical `type` values are centralized and versioned (example: `temperature_c`, `spot_price_sek_kwh`, `solar_irradiance_wm2`).

- Fetchers map provider fields -> canonical types.
- Writers must validate against registry before append.
- Registry implementation is SDK-owned and X-macro based (`src/sdk/canonical_types.def`).

## Endpoint artifact contract

- Location: `common.paths.endpoints_dir`
- File: `<endpoint_name>.json`
- Write policy: temp file + fsync + atomic rename
- Server serves artifacts as-is (after JSON validity check)

## Logging Contract

Unified multi-process log sink:

- Location: `common.paths.log_file`
- Format: JSONL
- Required fields:
  - `ts`
  - `level`
  - `category`
  - `msg`
  - `pid`
  - `proc`
  - `worker` (optional)
  - `file`
  - `line`
  - `config_version`

Write policy:

- `open(O_APPEND | O_CLOEXEC)`
- `write()` whole record line
- bounded max line size
- optional lock when fallback path requires it

Implementation module:

- `src/log/sunspots_log.h/.c`

## Sunspots SDK (Facade-Only)

`sunspots_sdk` is a thin worker-facing facade over lower-level libraries.

It provides:

- bootstrap/lifecycle
- heartbeat helper
- validated DB append/read
- endpoint artifact publisher
- structured logging helper wrappers

It does **not** own daemon orchestration and does **not** replace low-level modules.

## Server Contract

HTTP server behavior:

- Route: `GET /api/<name>`
- Maps to: `<endpoints_dir>/<name>.json`
- Response statuses:
  - `200` success
  - `404` missing endpoint
  - `405` invalid method
  - `413` endpoint payload too large
  - `500` invalid JSON/read failure

Runtime details:

- raw sockets + epoll
- endpoint artifacts cached in RAM
- refresh via inotify + periodic fallback scan

## Build Direction

Separate binaries (no multi-`main` linking):

- daemon target
- worker targets
- server target
- tests target

No temporary compatibility shims for old entrypoints.

## Implementation Phases

## P0 (Implemented)

1. Config slice loading in workers.
2. Daemon config runtime (slice build + versioning).
3. SDK v1 facade (bootstrap, heartbeat, DB emit/read, endpoint publish).
4. Project-native JSONL logging module wired in runtime components.
5. Runtime paths moved to config.
6. Standalone server binary with epoll + cache + inotify/fallback reload.

## P1 (Next)

1. DB rotation/compaction.
2. Inotify + fallback polling hardening.
3. Worker metrics (records, errors, latency).
4. End-to-end integration test pipeline.
5. Contract tests for schema/config validation.

## Done Criteria

- Daemon is supervisor-only in behavior and code boundaries.
- At least one full fetch->compute->serve pipeline runs through file contracts.
- New worker can be added by contract/config only (no changes to existing workers).
- Server serves newly published endpoint artifacts without restart.
- All active runtime paths are config-driven and versioned.
