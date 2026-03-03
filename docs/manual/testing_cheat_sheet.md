# Sunspots Testing Cheat Sheet

Quick copy/paste commands for common testing workflows.

## Build all, run all tests, run all checks
Populates log/BRANCH/ with reports. 
```bash
make all 
```

## 1) Kill Running Processes + Clean Runtime Data

```bash
pkill -f sunspots_daemon || true
pkill -f sunspots_backfill_openmeteo || true
pkill -f sunspots_fetch_ || true
pkill -f sunspots_compute_ || true
pkill -f sunspots_logger || true
pkill -f sunspots_frontend || true

```

## 2) Build (Debug)

All debug targets:

```bash
make build
```

Single module:

```bash
make build M=sunspots_backfill_openmeteo
```

Build tests only:

```bash
make build-tests
```

## 3) Run Tests

All debug tests:

```bash
make run-tests
```

Single test/module filter:

```bash
make run-tests M=sdk_log_module_test
```

Direct binary run:

```bash
build/debug/tests/sdk_config_module_test
build/debug/tests/sdk_db_module_test
build/debug/tests/sdk_log_module_test
build/debug/tests/backfill_worker_module_test
```

## 4) Valgrind Lane

```bash
make build-valgrind
make build-tests-valgrind
make run-tests-valgrind
```

Note:

1. `make run-valgrind` without `M=...` is currently a no-op with an informational note because daemon background mode is not reliable for Valgrind capture.
2. Use `make run-valgrind M=<module>` for real runtime Valgrind execution.

Module-specific:

```bash
make build-valgrind M=sunspots_compute_manager
make run-valgrind M=sunspots_compute_manager
```

## 5) Tidy / Static Analysis

```bash
make all
# OR run single analysis targets:
make tidy
make cppcheck
make lizard
make warnings
```

## 6) Manual Runtime Test (Daemon + Logs + DB)

Start daemon:

```bash
./build/debug/src/core/sunspots_daemon
```

Watch logs:

```bash
tail -f logs/daemon.log logs/sdk.log
```

Check DB file:

```bash
ls -lh db/*_*.db
```

SQLite quick checks:

```bash
DB=$(ls db/*_*.db | head -n1)
sqlite3 "$DB" ".tables"
sqlite3 "$DB" "select count(*) from records;"
sqlite3 "$DB" "select min(ts_start_utc), max(ts_start_utc) from records;"
```

## 7) Backfill One-Shot Manual Check

```bash
./build/debug/src/backfill/sunspots_backfill_openmeteo
echo $?
```

Expected:

1. Exit code `0`.
2. `logs/sdk.log` contains `backfill.complete`.
3. DB contains new weather records.

## 8) Output Paths

Make logs (per branch):

```text
logs/make/<branch>/raw_logs/
```

Common files:

1. `debug_configure.log`
2. `debug_build.log`
3. `debug_test.log`
4. `valgrind_build.log`
5. `valgrind_test.log`
6. `tidy_build.log`
7. `cppcheck.log`
8. `warnings_raw.log`
9. `../warnings_report.txt`
10. `../lizard_report.txt`

## 9) Useful Helpers

List runnable modules:

```bash
make list-modules
make list-modules-valgrind
```

Clean build trees:

```bash
make clean
make deepclean
```
