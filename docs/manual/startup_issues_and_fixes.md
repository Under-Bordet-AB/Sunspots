# Startup Issues and Fixes: Fetch/API Connectivity

This document explains what was broken in the startup path, why those failures happened, how each issue was fixed, and how to verify the fixes.

It is written as an engineering postmortem for developers who need precise cause-and-effect, not only a list of changed files.

## Symptoms observed

The following behavior was reported and reproduced:

- API worker binaries appeared to exit immediately at startup.
- Fetch manager startup was unstable and could crash.
- Daemon startup did not reliably connect to fetch manager when launched from build output.
- Process wiring felt disconnected because binary paths/config locations did not match current build target names.

These symptoms were a combination of configuration drift and runtime bugs.

## Root cause 1: stale binary paths in JSON config

The repository moved to CMake targets named:

- `sunspots_fetch_manager`
- `sunspots_fetch_openmeteo`
- `sunspots_fetch_elprisjustnu`

But the JSON config still referenced older names/paths.

### Why this broke startup

`fetch_manager` reads API `bin_path` values and uses `execv()` to launch them.
If `bin_path` points to old/nonexistent binaries, worker launch fails and the system looks disconnected.

### Fix applied

Updated `config/fetch_manager_config.json` paths to current executable names under the CMake output layout.

Also updated `config/sunspots.json` module `bin_path` for fetch manager to point to the current built executable path used in local development.

## Root cause 2: fragile config path assumption in fetch manager

`fetch_manager` previously loaded `fetch_manager_config.json` from the current working directory only.

### Why this broke startup

When started from a different CWD, config loading fails even if the file exists in the repository.

### Fix applied

In `src/fetch/fetch_manager.c`, config lookup now defaults to:

- `config/fetch_manager_config.json`

and supports override via:

- `SUNSPOTS_FETCH_MANAGER_CONFIG`

This makes startup independent of incidental CWD in common launch flows.

## Root cause 3: real crash in fetch manager path resolution

After adding API path resolution, `realpath()` was used without proper feature macros for prototype visibility.

### Why this crashed

Without the visible prototype, C treated `realpath()` as implicitly declared, leading to incorrect return-type handling and a bad pointer path that triggered ASan SEGV during `snprintf`.

### Fix applied

Added required feature macro in `src/fetch/fetch_manager.c`:

- `_XOPEN_SOURCE 700`

This exposes the correct `realpath()` declaration and removes the implicit-declaration runtime hazard.

## Root cause 4: daemon project root detection was wrong for build layout

The daemon previously inferred project root by walking `../..` from executable location.

### Why this broke startup

With CMake output paths (e.g. `build/debug/src/core/sunspots_daemon`), `../..` resolves to `build/debug`, not repository root. That breaks config discovery and module path resolution.

### Fix applied

`src/core/main.c` now walks upward from executable directory until it finds `config/sunspots.json`, then treats that directory as project root.

This makes daemon startup resilient to nested build output paths.

## Root cause 5: API write path dependency on pre-existing `.db` directory

API workers call `af_save()` in `atomic_file_rw.h`, which writes to `.db/database.jsonl`.

### Why this caused immediate exits

If `.db/` did not exist, `open()` failed, `af_save()` returned failure, and API workers exited.

### Fix applied

`src/libs/atomic_file_rw.h` now ensures parent directory creation before opening the file.

This removed the startup dependency on manual `.db` creation.

## Root cause 6: latent bug in fetch file reader helper

`read_file_to_string()` in `src/fetch/fetch_utils.h` freed memory on short read but continued execution and could write to freed buffer.

### Why this matters

Even if not always hit during startup, this is undefined behavior and can produce crashes/data corruption under read errors.

### Fix applied

Function now returns `NULL` immediately on read failure after freeing resources.

## Additional hardening

In `src/core/main.c`, daemon config reload now skips invalid module entries whose `bin_path` cannot be resolved, instead of partially populating invalid table entries.

This prevents bad module entries from corrupting process table state.

## Verification performed

After fixes:

- `make build` succeeds.
- `make test` succeeds.
- Running daemon starts fetch manager and API workers.
- API workers can write to `.db/database.jsonl` without pre-creating `.db`.

A new smoke target was added to automate this verification:

```bash
make smoke
```

Smoke check validates:

- fetch manager starts
- both API workers are running
- `.db/database.jsonl` receives data writes

## Operational notes

- Syslog-based components may print little/no stdout even when healthy.
- Quick timeout exits in manual tests can be expected when a process is killed by timeout, so exit code should be interpreted with context.
- Defunct (`Z`) processes observed after forced-kill daemon tests are zombie artifacts of abrupt termination and not active workers.
