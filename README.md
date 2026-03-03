# Sunspots

Sunspots is a multi-component C99/C++11 project with CMake as source of truth and a root `Makefile` wrapper for daily workflows.

## Requirements

- CMake 3.22+
- C compiler with C99 support
- C++ compiler with C++11 support
- POSIX threads

### Ubuntu/Debian packages

Minimum packages for `make build`:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  git \
  ca-certificates \
  libcurl4-openssl-dev \
  libglpk-dev \
  libsqlite3-dev
```

Notes:

- `libsqlite3-dev` is required by `find_package(SQLite3 REQUIRED)` (`sqlite3` CLI alone is not enough).
- GoogleTest and Google Benchmark are fetched automatically by CMake when missing.

Optional packages for extra workflows:

```bash
sudo apt install -y \
  sqlite3 \
  valgrind \
  clang-tidy \
  cppcheck \
  python3-pip \
  fzf \
  libgtest-dev \
  libbenchmark-dev
python3 -m pip install --user lizard
```

One-shot full developer setup (build + tests + warnings tools):

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  git \
  ca-certificates \
  libcurl4-openssl-dev \
  libglpk-dev \
  libsqlite3-dev \
  sqlite3 \
  valgrind \
  clang-tidy \
  cppcheck \
  python3-pip \
  fzf \
  libgtest-dev \
  libbenchmark-dev
python3 -m pip install --user lizard
```

## Build Policy

- CMake defines targets, dependencies, flags, tests, and benchmarks.
- The root `Makefile` provides developer-friendly commands.
- Per-folder Makefiles are not part of the active build system.

## Quick Start

```bash
make help
make build
make run-tests
make warnings
```

## Make Targets

```bash
make all
make build
make build-valgrind
make build-tests
make build-tests-valgrind
make list-modules
make list-modules-valgrind
make run
make run-valgrind
make run-tests
make run-tests-valgrind
make tidy
make cppcheck
make lizard
make warnings
make e2e
make e2e-valgrind
make clean
make deepclean
```

Notes:

- `make` defaults to `make build` (build only).
- `make all` runs a serialized full pipeline (build lanes + test lanes + tidy + reports).
- Most workflow targets accept optional `M=<module-or-target>` for module-scoped actions (examples: `M=daemon`, `M=fetch`, `M=sunspots_fetch_openmeteo`).
- `make list-modules` and `make list-modules-valgrind` dynamically discover runnable module targets from configured build trees.
- `make run` runs the normal daemon from existing debug builds and does not rebuild.
- `make run-valgrind` targets the daemon from existing valgrind builds and does not rebuild.
- `make run M=<module>` and `make run-valgrind M=<module>` run that module binary directly.
- `scripts/test_make_cli_matrix.sh` runs an automated CLI matrix across make targets, per-module `M=...`, and combined `M=...` cases.
- For a faster smoke run: `INCLUDE_RUN_TARGETS=0 INCLUDE_HEAVY=0 scripts/test_make_cli_matrix.sh`
- `make run` and `make run-tests` export `ASAN_OPTIONS`/`UBSAN_OPTIONS` with file logging to `logs/make/<branch>/raw_logs/asan/`.
- `make run-valgrind` (daemon default) prints an expected unreliability note and exits without starting Valgrind, because daemon background mode is currently incompatible with reliable capture.
- `make run-valgrind M=<module>` performs real Valgrind execution and writes per-process logs with child tracing.
- `make build-tests` and `make build-tests-valgrind` compile test binaries only.
- `make run-tests` and `make run-tests-valgrind` run tests only and do not rebuild.
- `make warnings` runs build lanes (`build`, `build-valgrind`, `tidy`) and regenerates warning/analysis reports.
- `make e2e` and `make e2e-valgrind` are placeholders and currently report "not yet implemented".

## Main Outputs

- Build trees:
  - `build/debug`
  - `build/valgrind`
- Logs:
  - `logs/make/<branch>/raw_logs/debug_configure.log`
  - `logs/make/<branch>/raw_logs/debug_build.log`
  - `logs/make/<branch>/raw_logs/debug_build_tests.log` (when `make build-tests` is run)
  - `logs/make/<branch>/raw_logs/debug_test.log` (when `make run-tests` is run)
  - `logs/make/<branch>/raw_logs/asan/asan.*` and `logs/make/<branch>/raw_logs/asan/ubsan.*` (sanitizer runtime logs from `make run`/`make run-tests`)
  - `logs/make/<branch>/raw_logs/valgrind_configure.log`
  - `logs/make/<branch>/raw_logs/valgrind_build.log`
  - `logs/make/<branch>/raw_logs/valgrind_build_tests.log` (when `make build-tests-valgrind` is run)
  - `logs/make/<branch>/raw_logs/valgrind_test.log` (when `make run-tests-valgrind` is run)
  - `logs/make/<branch>/raw_logs/run_valgrind_<target>.log` (when `make run-valgrind` is run)
  - `logs/make/<branch>/raw_logs/valgrind_tree/*.log` (per-PID daemon/subprocess Valgrind logs)
  - `logs/make/<branch>/raw_logs/tidy_configure.log`
  - `logs/make/<branch>/raw_logs/tidy_build.log`
  - `logs/make/<branch>/raw_logs/cppcheck.log`
  - `logs/make/<branch>/raw_logs/lizard.log`
  - `logs/make/<branch>/raw_logs/warnings_raw.log` (verbose bundle of report inputs)
  - `logs/make/<branch>/raw_logs/lizard_raw.log` (raw lizard console output copy)
  - `logs/make/<branch>/warnings_report.txt` (single ASCII report aggregating actionable warnings: compiler/clang-tidy/cppcheck/valgrind)
  - `logs/make/<branch>/lizard_report.txt` (separate complexity/recommendation report with tabular output)

## Executables

- `sunspots_daemon`
- `sunspots_daemon_dummy`
- `sunspots_frontend`
- `sunspots_fetch_manager`
- `sunspots_fetch_openmeteo`
- `sunspots_fetch_elprisjustnu`
- `sunspots_compute_manager`
- `sunspots_backfill_openmeteo`

## SDK Runtime Config

Sample runtime config keys live in `config/sunspots.json` under `system.sdk`:

- `db_dir`: SDK sqlite DB directory (default `db`; filename is derived from location)
- `log_level`: `debug`, `info`, `warn`/`warning`, `error`, `off`/`none` (default `debug`)
- `log_mirror_enabled`: mirror toggle (default `true`)
- `log_mirror_path`: mirror output path (default `logs/sdk.log`)

## Documentation

- SDK manual: `docs/manual/sdk.md`
- Backfill module manual: `docs/manual/backfill_openmeteo.md`
- Build and quality workflow: `docs/manual/build_and_quality_system.md`
- Static analysis ratcheting: `docs/manual/static_analysis_ratcheting.md`
- Bug tracker doc: `docs/bugs.md`
- Design notes: `docs/designs/SDK_public_db_API.md`
- Backfill design: `docs/designs/backfill_worker_design.md`

## Bug Reporting

1. Add it to `docs/bugs.md`, or
2. Open a GitHub issue.
