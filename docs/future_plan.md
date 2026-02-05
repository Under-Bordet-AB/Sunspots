# Sunspots Future Plan

Last updated: 2026-02-05

## Goal

Move from current validated prototype/runtime to production-ready architecture with robust data quality, scalability, and operations.

## Phase 1 - Data and SDK Hardening

- Add first-class `event_timestamp` in record envelope (separate from ingestion `timestamp`).
- Add `batch_id` for fetch-cycle grouping.
- Add canonical-type payload schema validation per type.
- Add explicit bad-line quarantine file and counters.
- Add reader offset/checkpoint mode for incremental calculators.

## Phase 2 - Daemon and Process Reliability

- Config change detection + targeted worker restarts (no full restart).
- Restart policies: exponential backoff + restart caps per worker.
- Graceful startup ordering (fetchers before calculators where needed).
- Add supervised health metrics (heartbeat jitter, restart count, uptime).

## Phase 3 - Server Robustness

- Add request parser hardening and header limits.
- Add endpoint cache metadata (mtime, size, parse status, last reload reason).
- Add optional worker thread pool for heavier request concurrency.
- Add structured server access logs + error logs with request IDs.

## Phase 4 - Storage Evolution

- Keep NDJSON as ingest log short-term.
- Introduce real storage backend (append log + queryable indexed store).
- Add compaction/retention policy and migration tooling.
- Add deterministic replay mode for calculators from historical snapshots.

## Phase 5 - Testing and Performance

- Add end-to-end integration tests with real process orchestration.
- Add chaos tests (kill/restart workers, file update races).
- Add load tests for endpoint server with cache churn.
- Add perf baseline and budget (CPU, RAM, response latency).

## Phase 6 - Operations and Delivery

- Add packaging/service files (systemd).
- Add runtime config docs with safe defaults per environment.
- Add observability dashboard from JSONL logs.
- Add release checklist + versioned migration notes.

## Immediate Next 3 Tasks

1. Implement record envelope `event_timestamp` + `batch_id` in SDK and providers.
2. Add daemon config-reload with targeted worker restart.
3. Add integration test: fetch sample -> compute endpoint -> API serve -> validate output.
