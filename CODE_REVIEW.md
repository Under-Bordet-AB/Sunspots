# Sunspots Code Review

Date: 2026-02-10

## Scope
- Full static review of current tracked project files under `src/`, `config/`, build files, and runtime supervision paths.
- Runtime sanity checks for daemon/server startup behavior.
- Build verification with `make debug` and `make test`.

## Findings (Ordered by Severity)

### Critical
1. CSV backend parsing is incorrect for escaped commas and can corrupt read/dedupe behavior.
- `src/sdk/internal/db/ss_db_internal.c:176` escapes commas as `\,`.
- `src/sdk/internal/db/ss_db_internal.c:262` splits fields using `strchr(..., ',')`, which still splits on escaped commas.
- Impact: any string field/value containing `,` can break row parsing, dedupe, and historical reads.

### High
1. Daemon can enter tight crash-respawn loops with no backoff.
- `src/core/sunspotsd.c:372` respawns immediately on `SIGCHLD`.
- No retry delay, no max restart rate, no circuit-breaker.
- Impact: CPU churn and log flood under persistent worker failures.

2. Server worker exits immediately when `endpoints` directory is missing, which triggers the crash loop above.
- `src/server/sunspots_server.c:413` requires `inotify_add_watch` on `endpoints_dir`.
- `src/server/sunspots_server.c:414`-`src/server/sunspots_server.c:420` exits on watch failure.
- In this repo state, `endpoints/` is absent, so daemon repeatedly respawns server.

3. Heartbeat supervision path is incompatible with the current server worker.
- Daemon stale check uses heartbeat timestamps: `src/core/sunspotsd.c:353`-`src/core/sunspotsd.c:369`.
- Server never emits heartbeat (`SIGRTMIN`) at all in `src/server/sunspots_server.c`.
- Impact: even when server stays up, daemon logic eventually treats it as stale and restarts it.

### Medium
1. Allocator mismatch risk (`free` vs `cJSON_free`) on JSON exported by config helpers.
- `config_export_subtree_json` returns memory from `cJSON_PrintUnformatted`: `src/config/config.c:489`-`src/config/config.c:507`.
- Freed with plain `free()` in:
  - `src/fetch/fetch_engine.c:741`
  - `src/fetch/fetch_engine.c:920`
  - `src/server/sunspots_server.c:371`
  - `src/workers/calc_smhi_avg_temp.c:174`
- Freed with `cJSON_free()` elsewhere (example: `src/core/sunspotsd.c:419`).
- Impact: currently may work with default allocators, but brittle and unsafe if cJSON hooks/custom allocators are used.

2. Worker parent signaling can hit unrelated processes after daemon death/PID reuse.
- Workers parse and store parent PID once (`src/fetch/fetch_engine.c:941`-`src/fetch/fetch_engine.c:945`, `src/workers/calc_smhi_avg_temp.c:190`-`src/workers/calc_smhi_avg_temp.c:194`).
- Workers continue sending `SIGRTMIN` to that PID (`src/fetch/fetch_engine.c:1118`, `src/workers/calc_smhi_avg_temp.c:243`).
- Impact: after daemon exits, PID reuse can send signals to unrelated processes.

3. Legacy runtime-slice module is compiled but no longer used by daemon execution flow.
- Built into `ss_core`: `CMakeLists.txt:15`.
- Not referenced by runtime daemon path anymore (`src/core/sunspotsd.c` now uses master config + worker name env model).
- Impact: stale logic surface and maintenance overhead.

4. Legacy calculator worker remains partially integrated and uses old poll model.
- `src/workers/calc_smhi_avg_temp.c:43`-`src/workers/calc_smhi_avg_temp.c:74` uses `poll_interval_*` keys (legacy style).
- Current project runtime has moved to aligned-slot supervision in fetch workers.
- Impact: conceptual drift and brittle future behavior when re-enabled.

5. No restart-rate control in daemon for any worker class.
- Respawn path (`src/core/sunspotsd.c:372`-`src/core/sunspotsd.c:382`) has no jitter/backoff/quarantine.
- Impact: one bad config or missing dependency can destabilize whole runtime quickly.

### Low
1. Test harness is intentionally removed, leaving no regression net.
- `Makefile:14` reports tests intentionally removed.
- Impact: higher risk of subtle runtime regressions during rapid development.

2. Status file error reason is not granular.
- Failed slot writes always use `"slot_deadline_missed"`: `src/fetch/fetch_engine.c:1113`-`src/fetch/fetch_engine.c:1115`.
- Impact: harder root-cause analysis between fetch/network failures and mapping/schema failures.

## Noted Unfinished/Brittle Areas
- `calc_smhi_avg_temp` is buildable but not aligned with the new supervision/scheduling architecture.
- Repo contains stale runtime artifacts (`runtime/config/*.json`) from previous slice-based flow.
- Logging and DB file migration is partially complete at runtime, but historical `.jsonl` artifacts remain and can confuse operational checks.

## Build/Test Status
- `make debug`: pass
- `make test`: pass (no-op target; tests removed)

## Recommended Execution Plan
1. Fix CSV parser to handle escaped separators correctly (or switch to robust quoting/JSONL/SQLite backend).
2. Add daemon restart backoff + max restart threshold + worker quarantine logging.
3. Ensure `endpoints_dir` exists before starting server, or auto-create it in server startup.
4. Add explicit heartbeat contract for non-deadline workers (or exclude server from stale-heartbeat supervision).
5. Standardize allocator ownership contract for config-exported JSON (`cJSON_free` consistently, or wrap with helper).
6. Remove or archive unused runtime-slice module and stale runtime/config artifacts.
7. Reintroduce focused tests for supervision, restart behavior, and DB read/write roundtrip with special characters.
