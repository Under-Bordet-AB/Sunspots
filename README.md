# Sunspots

Sunspots is a C99 project for a local "home energy brain": it collects time-series data (weather, energy prices, etc), stores it in a canonical form, and later uses that to compute decisions for a single home/location.

This repository is in heavy development. It contains multiple experimental binaries and partially-connected subsystems. This README aims to be truthful about what exists today.

## What Exists Today

### SDK (stable-ish for v1)

The SDK is the intended public integration surface for:

- writing canonical records to the DB backend
- reading records for recent history
- writing logs (plain text, one line per event)

Manual: `docs/manual/SDK.md`

Public header: `src/sdk/ss_sdk.h` (include via `#include "sdk/ss_sdk.h"`)

Under the hood in v1:

- DB is a blocking, file-backed TSV store (default `logs/ss_sdk_db.tsv`, override with `SS_SDK_DB_PATH`)
- logging appends to a plain-text file controlled by config (`system.log_path` inside `SUNSPOTS_CONFIG`)

### Daemon/supervisor (works, but not "productized")

There is a daemon-like supervisor in `src/core/main.c` that:

- reads `config/sunspots.json` (via the vendored `cJSON` library)
- spawns configured modules as child processes
- monitors children via a heartbeat signal and restarts on crash/hang
- hot-reloads when `config/sunspots.json` changes (inotify)
- exports each module's config JSON to the child as `SUNSPOTS_CONFIG`

### Fetchers and other modules (mixed)

There are fetch-related binaries and sources under `src/fetch/` and `src/fetch/apis/`. Some binaries are checked into the repo, but wiring them into "the full system" is still in progress (and varies by branch).

There are also other experiments (frontend, compute, server) under `src/` that are not yet connected into a coherent end-to-end flow.

## What Is Not Finished Yet

- A single, unified build that produces all intended binaries cleanly.
  - The repo currently contains multiple `main()` entrypoints under `src/` (daemon, fetch manager, API fetchers, frontend, etc).
  - That means a naive "compile everything into one binary" approach will fail at link time.
- An end-to-end pipeline where fetchers normalize provider responses into SDK canonical records and persist them via the SDK DB backend.
- A compute/optimizer module that consumes the canonical DB to produce actions.
- A proper HTTP/API surface (there is code under `src/frontend/`, but it is not a finished product).

## Configuration

Main config file: `config/sunspots.json`

The daemon reads this file and each child inherits a module-specific JSON payload as the `SUNSPOTS_CONFIG` environment variable.

### Note on large/nested configs in environment variables

Passing JSON via `SUNSPOTS_CONFIG` is convenient (every child gets the exact same config snapshot), but it has practical limits:

- Environment variables are size-limited by the OS (and share limits with process arguments). If the JSON grows large enough, `setenv()`/`exec*()` can fail or the process may not start with the expected environment.
- The config string is duplicated per-process, so very large configs waste memory across many child processes.
- The environment is relatively easy to inspect (for example via `/proc/<pid>/environ`), so avoid placing secrets directly in the JSON if the host isn’t trusted.

If config starts getting big, the usual next step is to pass a config *path* (or a small identifier) via the environment and have children read the file themselves, or move to a shared config service.

Important key used by the SDK:

- `system.log_path`
  - If this is an empty string, SDK logging is disabled.
  - If this is a path, SDK logging appends plain-text lines to that file.

## The `src/config` Module (Why It Feels Unused)

There is a separate config module under `src/config/` that implements a "subtree-based" config API on top of `cJSON`.

At the moment, the daemon does not use it. `src/core/main.c` parses `config/sunspots.json` directly with `cJSON` and passes JSON strings around.

Think of `src/config/` as an experimental library that may be adopted later, not as the current source of truth.

## Building (Current Reality)

Sunspots uses CMake + Ninja to build multiple executables from one repo. The simplest workflow is the tiny `Makefile` wrapper.

Build (debug + sanitizers):

```bash
make
```

Build a release:

```bash
make release
```

Build docs for developers (how the build works):

- `docs/manual/BUILD.md`

## Repo Pointers

- SDK manual: `docs/manual/SDK.md`
- Build manual: `docs/manual/BUILD.md`
- SDK code: `src/sdk/`
- Daemon/supervisor: `src/core/main.c`
- Fetchers: `src/fetch/`, `src/fetch/apis/`
- Vendored libs: `src/libs/`
