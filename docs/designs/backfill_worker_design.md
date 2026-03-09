# Backfill Worker Design (Open-Meteo Historical Data)

## 1. Overview

This document defines a startup-triggered backfill process that fills missing historical weather data from Open-Meteo into the SDK database.

Key policy decisions:

1. The backfill runs as a separate process forked by the daemon.
2. Daemon startup is non-fatal with respect to backfill outcomes.
3. Backfill reports status to daemon logging.
4. Calculators own data-readiness checks and must validate they have required data before computing.

## 2. Goals

1. Ensure historical data is available for calculators.
2. Fill only missing ranges ("holes"), not already-covered data.
3. Be safe to run on every program start.
4. Use SDK logging for milestone/progress reporting.
5. Respect Open-Meteo API constraints (timeouts, retries, rate limiting).

## 3. Non-Goals

1. Enforcing calculator readiness policy inside daemon or backfill.
2. Making daemon startup block/fail on backfill status.
3. Streaming every individual fetch/write as a log line.

## 4. Runtime Model

### 4.1 Daemon responsibilities

1. Spawn `backfill-worker` once at daemon startup.
2. Pass module config through `SUNSPOTS_CONFIG` and shared settings via `SUNSPOTS_SYSTEM`.
3. Continue daemon operation regardless of backfill result.
4. Record backfill status events in daemon logs.

### 4.2 Backfill-worker responsibilities

1. Read config and initialize SDK/Open-Meteo clients.
2. Query DB coverage and hole ranges for configured window.
3. Fetch missing ranges in chunks.
4. Upsert normalized records.
5. Re-verify hole coverage.
6. Report final status and exit.

## 5. Worker State Flow

1. `INIT`
2. `ANALYZE_GAPS`
3. `FETCH_WRITE_LOOP`
4. `VERIFY`
5. `COMPLETE` or `PARTIAL` or `FAILED`

The daemon timer is responsible for any later reruns.

## 6. Runtime Cadence

1. Run once when spawned.
2. Exit after reporting final status.
3. If recurring runs are desired, the daemon should respawn the worker by timer schedule.

## 7. Config Contract (Module Block)

Add a dedicated backfill module in `config/sunspots.json`.

Example shape:

```json
{
  "name": "BackfillOpenMeteo",
  "bin_path": "./build/debug/src/backfill/sunspots_backfill_openmeteo",
  "Timer-type": 1,
  "Rel-time": 86400,
  "start_immediately": true,
  "backfill": {
    "enabled": true,
    "start_date_utc": "2025-01-01",
    "chunk_days": 7,
    "retry_max_attempts": 5,
    "retry_base_backoff_ms": 1000,
    "freshness_lag_minutes": 120,
    "request_interval_ms": 250,
    "max_requests_per_minute": 240,
    "max_requests_per_hour": 2000,
    "max_requests_per_day": 8000,
    "progress_log_interval_sec": 10,
    "endpoint": "https://archive-api.open-meteo.com/v1/archive",
    "usage_daily_path": "logs/backfill_usage_daily.log"
  }
}
```

Notes:

1. `start_immediately=true` lets daemon spawn this timer module once at daemon startup.
2. `start_date_utc` defines the beginning of the backfill window.
3. Shared SDK config belongs under top-level `system.sdk`, not inside the module block.

## 8. SDK API Extensions Needed

Current SDK has single-record write and canonical window reads. Backfill needs explicit gap introspection and efficient writes.

Proposed additions:

1. `ss_sdk_db_get_coverage(...)`
2. `ss_sdk_db_get_holes(...)`
3. `ss_sdk_db_write_batch(...)` (or transactional wrapper over write-one)
4. `ss_sdk_db_verify_complete(...)`

Suggested semantics:

1. Coverage reports expected slots vs present slots and percentage.
2. Holes returns contiguous missing ranges with minimum-gap filtering.
3. Batch write is idempotent and reports inserted/updated/rejected counts.
4. Verify-complete returns complete/incomplete plus unresolved hole ranges.

## 9. Reporting and Logging

Use SDK logging macros/fields. Log milestones and periodic progress, not every record.

Event set:

1. `BACKFILL_STARTED`
2. `BACKFILL_ANALYZE_DONE`
3. `BACKFILL_PROGRESS`
4. `BACKFILL_RATE_LIMITED`
5. `BACKFILL_RETRY`
6. `BACKFILL_VERIFY_OK`
7. `BACKFILL_PARTIAL`
8. `BACKFILL_FAILED`

Required final fields:

1. `run_id`
2. `window_from_utc`, `window_to_utc`
3. `holes_before`, `holes_after`
4. `records_written`, `chunks_done`, `chunks_total`
5. `duration_ms`
6. `status`

## 10. Open-Meteo Fetch Policy

1. Chunk requests by date range (`chunk_days`).
2. Apply rate limiter windows before each request:
3. `max_requests_per_minute` (capped by Open-Meteo published 600/min)
4. `max_requests_per_hour` (capped by Open-Meteo published 5000/hour)
5. `max_requests_per_day` (capped by Open-Meteo published 10000/day)
6. Retry on transient failures with backoff and stop on exhaustion.

## 11. Failure Handling

1. Daemon is never terminated by backfill status.
2. Backfill communicates `COMPLETE`, `PARTIAL`, or `FAILED`.
3. Unresolved holes are logged with explicit ranges.
4. Calculators decide whether to proceed, degrade, or fail based on their own data checks.

## 12. Calculator Boundary

Calculator modules must validate needed data windows before computing.

Backfill is best-effort data preparation, not a calculator correctness gate.

## 13. Acceptance Criteria

1. On startup, daemon spawns backfill-worker as separate process.
2. Worker can detect no-work condition and exit/report quickly.
3. Worker fills only missing ranges and writes idempotently.
4. Worker reports periodic progress and final status through SDK logger.
5. Daemon remains alive on backfill partial/failure and logs outcome.
6. Repeated hole checks are achieved by daemon timer respawns, not an internal maintenance loop.
7. Calculators remain responsible for verifying required data availability.
