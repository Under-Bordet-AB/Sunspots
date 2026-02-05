# Sunspots Project Bearings

Last updated: 2026-02-03

## Quick snapshot

- Branch: `develop`
- HEAD: `9c7e4b3` (`origin/develop`)
- Primary language: C99
- Build system: `make` (`debug`, `release`, `test`, `lint`, `format`, `run`)
- Repo style: watchdog/supervisor + worker process prototypes, with config and fetch libraries under active development

## Project intent (from docs + raw notes)

Sunspots aims to optimize solar/battery/grid usage by combining weather + spot-price data into a time-based energy plan (24-72h), exposed to clients via an API/CLI.

The `raw_docs/` content describes the target architecture as a pipeline:

`fetch -> transform/parse -> compute -> store/cache -> deliver`

## Current codebase map

### Stable-ish foundation

- `src/config/config.c` + `src/config/config.h`
  - Opaque config handle over cJSON.
  - Supports file/env/CLI loading + dot-path lookups.
  - Includes safe/default and strict getters.
- `tests/test_config.c` + `tests/test_args.c`
  - Config module tests are present and reasonably complete for current features.

### Active prototype areas

- `src/core/main.c`
  - Daemonization + process watchdog loop.
  - Tracks worker health by heartbeats (`SIGRTMIN`).
  - Respawns dead/hung workers.
- `src/core/fetch_data.c`
  - Worker with `epoll` + `timerfd` + `inotify`.
  - Sends periodic heartbeats to parent.
  - Watches one hardcoded file path for writes.
- `src/core/sunspots_core.c`
  - Explicitly marked test code.
  - Sends heartbeats and writes debug output.

### Early/placeholder modules

- `src/fetch/fetch_manager.c`
  - Skeleton threading loop with stub fetch functions.
- Empty placeholder files:
  - `src/compute/file`
  - `src/database/file`
  - `src/transform/file`
  - `src/utils/file`
  - `src/fetch/tcp/file`
  - `src/fetch/tls/file`

### Dependencies and support code

- Vendored JSON: `src/libs/json/cJSON.c`, `src/libs/json/cJSON.h`
- Git submodule logger: `src/libs/jj_log`
- Single-header atomic file helper: `src/libs/atomic_file_rw.h`
- Custom banned API scanner: `scripts/check_banned.sh`

## Build/test reality (commands run today)

### `make debug`

Current status: fails at link step.

Main causes:

1. Multiple `main()` definitions are included in one binary link:
   - `src/core/main.c`
   - `src/core/sunspots_core.c`
   - `src/core/fetch_data.c`
   - `src/fetch/fetch_manager.c`
   - `src/libs/jj_log/test_jj_log.c`
2. Missing math linkage (`-lm`) for `sqrt`/`pow` used by `src/core/sunspots_core.c`.

### `make test`

Current status: misleading pass behavior.

- The `$(TEST_BIN)` recipe in `makefile` prints `[PASS]` even if the link command fails.
- Follow-on output can show linker errors while test stage still reaches `No tests found.`
- Root cause is shell flow in `makefile` where command failure is not guarded before the success `printf`.

### `make lint`

Current status: fails quickly in banned check.

- Real findings: banned APIs are still in runtime C sources (`sprintf`, `atoi`, `printf`, `fprintf`, etc.).
- Noise findings: scanner also catches banned words in markdown content under `src/core/*.md` because scan scope is entire `src`.

## Important gotchas to remember

1. `make format` rewrites most C/H files under `src/` (including submodule files), so run it intentionally.
2. `src/core/fetch_data.c` has a hardcoded absolute watch path:
   - `"/home/drone/Code/c/system_c_cpp/boiler_room_prj/Sunspots/src/core/output.envar"`
3. `src/core/main.c` currently relies on cwd-derived binary paths and `setenv("TEST", ...)` during spawn.
4. The standards docs are strict (ban list + jj_log requirement), but implementation is mid-migration.

## Suggested next steps (highest leverage)

1. Fix build graph first:
   - Split binaries by target (daemon, workers, tests) instead of linking all `src/*.c` into one output.
   - Add `-lm` where needed.
2. Make test failures truthful:
   - Ensure link/test command failures stop the target immediately.
3. Replace hardcoded runtime paths with config-driven values.
4. Continue standards migration:
   - Replace banned APIs.
   - Route output through `jj_log` where required.

## Handy reference files

- Entry orientation: `README.md`
- Build pipeline: `makefile`
- Coding rules: `docs/standards/code.md`
- Banned identifiers: `docs/standards/banned.md`
- Config design/manual: `docs/design/configuration.md`, `docs/manual/config.md`
- Supervisor logic: `src/core/main.c`
- Worker prototype: `src/core/fetch_data.c`
- Config implementation: `src/config/config.c`
