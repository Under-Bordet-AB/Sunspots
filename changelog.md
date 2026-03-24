# Changelog: Non-SDK / Non-Backfill Changes

Branch: `feat/backfill-single-runs-history`  
Generated: 2026-03-04 (UTC)

This document lists all current working-tree changes outside `src/sdk/**` and `src/backfill/**`.

## Runtime / Build

### `Makefile`
- Added `kill` target (and alias `kill-sunspots`) to stop all Sunspots processes.
- `make help` now documents `make kill`.
- `make run` now prints:
  - success marker when daemon starts
  - guidance: `use 'make kill' to exit`
- `make kill` behavior:
  - sends SIGTERM to daemon first (graceful path)
  - force-kills remaining live `sunspots_*` / `backfill` processes if needed
  - reports zombie count

## Configuration

### `config/sunspots.json`
- Moved SDK config from top-level `"sdk"` to `"system.sdk"`:
  - `"db_dir"`
  - `"log_level"`
  - `"log_mirror_enabled"`
  - `"log_mirror_path"`
  - `"log_mirror_max_bytes"`
- Fixed module timer key typo for Elpris:
  - `"Rek-time"` -> `"Rel-time"`

Impact:
- Aligns runtime config shape with codepath expecting `system.sdk`.
- Ensures Elpris module runs on configured relative timer instead of default fallback.

## Weather Transform (Non-SDK)

### `src/transform/weather/openmeteo.c`
- Updated solar transform selection logic for observation flow:
  - previously selected latest hourly point (could be future)
  - now selects nearest non-future point (`<= now`)
  - if none exists, falls back to earliest future point
- Preserves already-set timestamp from current weather when available.
- Added required includes:
  - `<stdint.h>`
  - `<time.h>`

Impact:
- Prevents observation rows from being stamped into future slots.
- Restores compute pipeline readiness for near-now observation windows.
- Enabled `endpoints/forecast.json` and `endpoints/result.json` generation after fix.

## Excluded On Purpose

Not listed here:
- `src/sdk/**` (SDK code)
- `src/backfill/**` (backfill code)
- SDK-focused test/doc edits directly tied to SDK refactors
