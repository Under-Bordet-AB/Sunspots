# Backfill

## Purpose

Backfill is the repair and historical-ingestion worker.

Its job is:

- find missing weather history in the SDK DB
- fetch archive data for holes
- fetch historical forecast runs
- write canonical records through the SDK
- verify whether the requested history window is now complete

Operationally, it is a daemon-managed timer worker, not a library or in-process service.

## Key files

- `src/backfill/backfill_openmeteo.c`
- `src/backfill/backfill_config.c`
- `src/backfill/backfill_payload.c`
- `src/backfill/backfill_holes.c`
- `src/backfill/backfill_common.c`

## How it works

Startup:

- requires `SUNSPOTS_CONFIG` and `SUNSPOTS_SYSTEM`
- reads one backfill module config from `sunspots.json`
- reads shared location/SDK config from `system`

Runtime flow:

1. parse config
2. analyze current DB holes
3. fill missing archive ranges in chunks
4. optionally fetch historical forecast runs
5. verify the window again
6. exit

The daemon provides repetition by respawning it on the timer. Backfill itself is single-run per process.

## Design strengths

- The role is clear and operationally sensible.
- It now uses the SDK correctly rather than maintaining a second persistence model.
- The refactor into `common/config/payload/holes` improved the structure a lot.
- The test direction is good: real DB assertions, not just exit code checks.

## Main weaknesses

- `backfill_openmeteo.c` is still the orchestration center for too many concerns.
- Forecast-history logic is better than before, but still somewhat policy-dense.
- Hole scanning still encodes a chunk/window policy internally.
- This module is tightly coupled to the current Open-Meteo payload shape and metric subset.

## Critique

Backfill used to look like a monolithic worker. It is substantially better now.

The biggest improvement is not cosmetic; it is that the worker now has identifiable sub-responsibilities:

- config parsing
- hole detection
- payload mapping
- orchestration

That is what makes it testable.

The remaining architectural issue is that orchestration still owns too much policy:

- rate limiting
- progress/dashboard output
- archive fill loop
- forecast-history loop
- verification and final exit semantics

That is acceptable for one worker, but it is still the part most likely to become brittle if more providers or more backfill types are added.

## What is good now

- daemon-only startup contract
- single-run process model
- `sunspots.json` as the single config source
- one `start_date_utc`, fill forward
- real DB-backed tests
- reusable helper modules instead of a giant one-file worker

## What still needs care

- forecast-history enable/disable logic should key off the actual single-run source, not the archive source
- path/config values that leave the module boundary should be treated more defensively
- hole-scan policy could be named and documented more explicitly
- if price backfill is added, shared backfill abstractions should grow carefully instead of cloning this worker

## Quality verdict

Good worker design now, no longer junior-looking, but still an orchestration-heavy module rather than a fully generalized backfill framework.
