# Sunspots

Sunspots is a multi-component C99/C++11 project with a unified CMake build system and a root `Makefile` wrapper for daily development.

## Build system policy

- CMake is the only build source of truth.
- The root `Makefile` is a convenience wrapper.
- Legacy per-folder make build scripts have been removed.

## Requirements

- CMake 3.22+
- C compiler with C99 support
- C++ compiler with C++11 support
- libcurl
- pthreads (POSIX)
- Optional: valgrind, clang-tidy

## Quick start

```bash
make build
make test
```

## Make targets

Use `make <target>` with the following targets:

```bash
make
make configure
make build
make rebuild
make test
make test-unit
make test-integration
make test-component COMPONENT=sample
make bench
make smoke
make valgrind
make run COMPONENT=sunspots_frontend
make run COMPONENT=sunspots_daemon ARGS="daemon"
make tidy
make clean
make deepclean
```

## Build outputs

By default the wrapper uses `build/debug` and Debug configuration with sanitizers enabled.

## Component executables

- `sunspots_daemon`
- `sunspots_frontend`
- `sunspots_fetch_manager`
- `sunspots_fetch_openmeteo`
- `sunspots_fetch_elprisjustnu`
- `sunspots_compute_manager`

## Testing and quality model

CTest labels are used for filtering:

- `unit`
- `integration`
- `perf`
- `valgrind`
- `component:<name>`

Current test/benchmark files are intentionally named as placeholders:
- `tests/unit/sample_test.cpp`
- `benchmarks/sample_benchmark.cpp`

## Documentation

- `docs/manual/build_and_quality_book.md`
- `docs/manual/startup_issues_and_fixes.md`
- Existing component docs remain under `docs/manual/`
