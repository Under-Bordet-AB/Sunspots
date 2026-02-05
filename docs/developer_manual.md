# Sunspots Developer Manual

Last updated: 2026-02-05

## Overview

Sunspots is a multi-process C system with file-based business-data flow:

`fetch -> normalize -> NDJSON DB -> compute -> endpoint artifact -> HTTP serve`

## Binaries

- `sunspotsd`: supervisor daemon
- `fetch_openmeteo`: Open-Meteo ingestion worker
- `fetch_smhi`: SMHI ingestion worker
- `calc_smhi_avg_temp`: SMHI prognosis average temperature calculator
- `sunspots_server`: API server (`GET /api/<name>`)

## Build and Run

```bash
make all
make test
make run
```

`sunspotsd` auto-loads `config/sunspots.json`.

## Architecture Notes

- Daemon monitoring is signal-driven (`SIGRTMIN` heartbeats + `SIGCHLD`).
- Server is event-driven (`epoll`) and refreshes endpoint cache with `inotify` + fallback scan.
- Workers use `sunspots_sdk` for bootstrap, heartbeat, record emit/read, and endpoint publish.
- Logging uses project-native `src/log/sunspots_log.*` (JSONL output).

## Config Model

Key config sections:

- `common.location.*` (shared coordinates)
- `common.paths.*` (`db_file`, `endpoints_dir`, `log_file`)
- `common.heartbeat.*`
- `workers.<name>.*`

Poll interval options per worker:

- `poll_interval_seconds`
- `poll_interval_minutes`
- `poll_interval_hours`
- or `poll_interval.value` + `poll_interval.unit`

## Data Contracts

### DB (NDJSON)

Required envelope fields:

- `version`
- `source`
- `type`
- `timestamp`
- `payload`

Canonical type registry:

- `src/sdk/canonical_types.def`
- `src/sdk/canonical_types.h/.c`

### Endpoint Artifacts

- Path: `common.paths.endpoints_dir`
- File: `<endpoint>.json`
- Writer behavior: temp file + `fsync` + `rename`

### Logs

- Path: `common.paths.log_file`
- Format: one JSON object per line
- Required metadata includes: `ts`, `level`, `category`, `pid`, `proc`, `worker`, `file`, `line`, `config_version`

## Testing

Current test entrypoint:

```bash
make test
```

Coverage includes:

- config loading/access
- SDK emit/read/publish paths
- provider mapping for Open-Meteo and SMHI

## Operational Debugging

- Missing endpoint: check `endpoints/` and calculator worker status.
- Worker restart loop: inspect `logs/sunspots.jsonl` + worker slices in `runtime/config/`.
- No DB growth: verify fetch worker config and `common.paths.db_file`.
