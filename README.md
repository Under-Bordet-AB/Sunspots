# Sunspots

Sunspots is a multi-component C99/C++11 project with a unified CMake build system and a root `Makefile` wrapper for daily development.

## Bug reporting

If you find a bug:

1. Add it to `docs/bugs.md`.
2. Or open a GitHub issue.

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
- Optional: valgrind, clang-tidy, clang/clang++ (libFuzzer), AFL++ (afl-fuzz)

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
make valgrind
make run COMPONENT=sunspots_frontend
make run COMPONENT=sunspots_daemon ARGS="daemon"
make tidy
make fuzz-build
make fuzz-run TARGET=http_request_fuzzer
make fuzz-all
make clean
make deepclean
```

`make tidy` is optional and only runs when invoked explicitly.

## Fuzzing

Default fuzzing engine is libFuzzer.

```bash
make fuzz-build
make fuzz-run TARGET=http_request_fuzzer
make fuzz-all
```

`FUZZ_TIME` defaults to `60` seconds per target. Override when needed (example: `FUZZ_TIME=300`).
Current fuzz targets include:
- `http_request_fuzzer`
- `openmeteo_transform_fuzzer`
- `config_args_fuzzer`
- `compute_calculator_fuzzer`
- `fetch_manager_config_fuzzer`

Optional AFL++ mode:

```bash
make fuzz-build FUZZ_ENGINE=afl
make fuzz-run FUZZ_ENGINE=afl TARGET=http_request_fuzzer
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
- `valgrind`
- `component:<name>`

Current reference files:
- `tests/unit/sample_test.cpp` (minimal sample)
- `tests/unit/config_module_test.cpp` (module-focused config test example)
- `benchmarks/sample_benchmark.cpp`

## Documentation

- Existing component docs under `docs/manual/`
- Build/test/benchmark bug review: `docs/bugs.md`
- Fuzzing findings: `docs/fuzz.md`

## External references

- CMake documentation: `https://cmake.org/documentation/`
- GoogleTest documentation: `https://google.github.io/googletest/`
- Google Benchmark documentation: `https://github.com/google/benchmark/blob/main/docs/user_guide.md`
- libFuzzer documentation: `https://llvm.org/docs/LibFuzzer.html`
- AFL++ documentation: `https://aflplus.plus/docs/`
