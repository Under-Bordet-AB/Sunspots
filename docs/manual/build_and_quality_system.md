# Sunspots Build and Quality System

This document is written for C developers who are new to CMake, structured testing, and practical benchmarking in a multi-program repository.

You already know how to write C. The goal here is to make the repository feel predictable: you should be able to answer, without guessing, where build behavior is defined, how executables and libraries are wired together, how to add tests, and how to run performance checks without turning numbers into superstition.

Read this top-to-bottom once. After that, use it as a reference when you add a component, introduce a new dependency, or want to verify that your changes still fit the project conventions.

## Chapter 1: Why we need a build and quality system

A small C project can get away with one Makefile and a few source files. Sunspots has a different shape: it contains multiple executables, shared libraries, external dependencies, and a workflow that must work the same way for every developer and in CI.

The hard problem is not "how do I compile a `.c` file?". The hard problem is "how do we keep build flags, link dependencies, tests, and tools consistent while the codebase changes?". That is why this project uses CMake as the source of truth, and a small root `Makefile` as a convenience interface.

CMake answers: what exists, how it compiles, what it links, and what tools apply to which targets.
The root `Makefile` answers: what commands developers type every day.

## Chapter 2: The two-layer model (CMake + Make wrapper)

Think in two layers.

Layer 1 is the declaration layer: CMake reads `CMakeLists.txt` files and produces a build graph. This is where targets are defined, libraries are linked, compile options are set, and tests/benchmarks are registered.

Layer 2 is the usage layer: the root `Makefile` runs CMake and CTest with consistent arguments so the team can type short commands like `make build`, `make run`, or `make run-tests`.

If you ever see a mismatch, fix the CMake layer. The wrapper should never be a second build system.

Recommended reading order for understanding the build system:
- `CMakeLists.txt`
- `cmake/SunspotsOptions.cmake`
- `cmake/SunspotsDependencies.cmake`
- `src/CMakeLists.txt`
- `Makefile`

Now run:

```bash
make
```

If that works, the build system is at least configured and can produce binaries from a clean checkout.

## Chapter 3: How targets map to the codebase

The most important habit in a multi-program repository is separating "code you want to test" from "code that starts a process".

Executable targets are entrypoints. They handle arguments, environment, process lifetime, and OS integration.

Library targets are reusable logic. They are what unit tests and benchmarks should link against. When you keep most behavior in libraries, you can test it without forking processes or relying on syslog output.

In this repository, `src/CMakeLists.txt` is the directory index that pulls in component-level `CMakeLists.txt` files. Each component `CMakeLists.txt` should make it obvious what it builds and why. If a component feels mysterious, start by reading its local `CMakeLists.txt` and then follow the targets it links.

## Chapter 4: What happens when you run `make`

The default `make` goal is `build`. That means plain `make` is build-only.

Conceptually, the daily loop is:

1. Build app lane: `make build`
2. Run app lane: `make run`
3. Build tests only (when iterating tests): `make build-tests`
4. Run tests: `make run-tests`
5. Warning reports: `make warnings`

Module-scoped loop (optional):
- Discover runnable targets: `make list-modules`
- Build a specific module: `make build M=daemon` (or any listed alias/target)
- Run a specific module: `make run M=sunspots_fetch_openmeteo`
- Valgrind a specific module: `make run-valgrind M=fetch_openmeteo`
- Run full CLI matrix automation: `scripts/test_make_cli_matrix.sh`

Important detail: `make run-tests` runs CTest in the existing build directory and does not rebuild test binaries. If you changed test/source files, run `make build-tests` first.

Valgrind loop (separate lane):
- Runtime checks on tests: `make build-tests-valgrind` then `make run-tests-valgrind`
- Runtime checks on daemon: `make build-valgrind` then `make run-valgrind`

`make run-valgrind` is preconfigured to trace child processes and write per-PID logs under `logs/make/<branch>/raw_logs/valgrind_tree/`.

Current limitation: daemon-level Valgrind is not reliable while the daemon runs in background mode. Child-process logs are still captured in `valgrind_tree/`. Complete daemon-level capture requires a foreground-capable daemon mode.

For a single serialized pass, use `make all`. It runs the main build/test/report pipeline under a lock file in `logs/make/<branch>/raw_logs/` so overlapping `make all` or `make warnings` executions fail fast instead of colliding.

`make warnings` is a consolidated actionable-diagnostics report, not just compiler output. It runs build lanes (`build`, `build-valgrind`, `tidy`) before generating reports, then aggregates parsed diagnostics from compiler/clang-tidy/cppcheck/valgrind lanes when available.

The configure step may print messages about fetching GoogleTest or Google Benchmark. That simply means those packages were not found via the system package manager, so CMake is bootstrapping them using `FetchContent`. This is expected in this project.

## Chapter 5: Standards, warnings, and sanitizers

Sunspots targets C99 and C++11. This is not left to compiler defaults.

Warnings are enabled to catch common mistakes without turning the build into a constant fight. The goal is to keep warnings meaningful and actionable, not to enforce perfection at the cost of velocity.

Debug builds enable sanitizers by default. Sanitizers are not a replacement for tests, but they are excellent at making classes of memory and undefined-behavior defects visible during everyday development. When a sanitizer report happens, treat it as a correctness bug, not as a "tool issue".

In this repository, `make run` and `make run-tests` set `ASAN_OPTIONS`/`UBSAN_OPTIONS` with `log_path` under `logs/make/<branch>/raw_logs/asan/` so reports are preserved even when daemonized/background processes close terminal streams.

## Chapter 6: Dependencies without mystery

External dependencies are resolved with a simple rule:

1. Prefer system packages when available.
2. If a developer machine (or CI) does not have them, bootstrap a known version for test/benchmark dependencies.

In practice, `cmake/SunspotsDependencies.cmake` is where to look. It contains all the "how we find or fetch" logic.

This matters because it makes onboarding simpler: a new developer can build without spending half a day learning which packages are missing. It also matters because it makes CI reproducible: the build graph always has a path forward.

## Chapter 7: Testing in this repository

Tests are built as normal executables, and then registered with CTest. Use `make build-tests` (or `make build`) to compile test binaries, then `make run-tests` to execute them through CTest.

Current module-focused examples live under:
- `tests/unit/sdk_db_module_test.cpp`
- `tests/unit/sdk_canonical_module_test.cpp`
- `tests/unit/sdk_log_module_test.cpp`
- `tests/unit/sdk_config_module_test.cpp`

Run:

```bash
make build-tests
make run-tests
make build-tests-valgrind
make run-tests-valgrind
```

Then open:
- `tests/CMakeLists.txt`
- one SDK test file from `tests/unit/`

Read the structure and notice the intent:
- it arranges deterministic inputs
- it calls one function
- it asserts a small number of behavior-relevant outputs

That is the pattern to copy.

## Chapter 8: Why the tests are written in C++

Even though much of the project is C, the test framework is GoogleTest, which is C++.

The practical reason is that GoogleTest is mature and ergonomic, and it lets the repository use one consistent test framework for both C and C++ targets.

The bridge is straightforward: include C headers under `extern "C"` in your test `.cpp` file. That keeps your C functions callable while still using a C++ test runner.

If you see a test file including C headers with `extern "C"`, it is not "making the code C++". It is simply giving the linker the correct symbol names.

## Chapter 9: Benchmarking (what it is and how to use it)

Benchmarking is not a correctness check. Benchmarks are measurement instruments.

Correctness answers: "Is behavior right?"
Benchmarking answers: "How expensive is this path?"

Sunspots ships a deliberately named placeholder benchmark as a template:
- `benchmarks/sample_benchmark.cpp`

Run:

```bash
make build
./build/debug/benchmarks/sample_benchmark
```

Google Benchmark works by executing a benchmark function many times. You write a function that accepts `benchmark::State& state`, and then you place the code you want to measure inside:

`for (auto _ : state) { /* measured code */ }`

That loop is the core contract. The framework (not you) decides how many iterations to run so it can produce a stable estimate.

When the benchmark runs, the output includes machine context (timestamp, CPU, cache sizes, load average) followed by one row per benchmark. A row looks like:

`BM_SampleCalculateSimple   <Time>   <CPU>   <Iterations>`

`Time` is wall-clock time per iteration and includes scheduler noise.
`CPU` is CPU time per iteration and is usually the better signal for algorithmic cost.
`Iterations` is how many iterations were executed to estimate the result.

Input selection is what separates a professional benchmark from a toy. Use inputs that represent real workload shapes and force the branch paths you care about. If there is a common fast path and a rare expensive path, write two separate benchmarks so you can see each cost clearly. Keep IO and network calls out of the measured loop unless you are explicitly measuring end-to-end latency.

You will also see `benchmark::DoNotOptimize(...)` and `benchmark::ClobberMemory()` in the template. They exist to reduce unrealistic compiler optimizations that would otherwise remove work and produce misleading numbers.

## Chapter 10: Mini tutorial - writing a professional test

A professional test starts with a behavior sentence, not a file name. Pick one function with stable inputs/outputs and write down the contract in plain language. For example: "When solar availability is high, the decision should prefer solar and avoid buying electricity."

Then write a test that enforces that contract.

Create a new test file such as `tests/unit/compute_decision_test.cpp`, use the same `extern "C"` include style as the sample, and keep one test focused on one behavior. Avoid randomness. Avoid asserting internal details that will change during refactors. Assert the smallest set of output fields that make the behavior unambiguous.

After writing the test, register it in `tests/CMakeLists.txt` as a new executable and a new CTest entry, then run:

```bash
make build-tests
make run-tests
```

If it fails, don’t "fix" the test immediately. Decide whether the code is wrong or the behavior sentence was wrong. The value of tests is that they force you to make that decision explicitly.

## Chapter 11: Mini tutorial - running a professional performance test

Professional performance work starts with a question. Example: "Does this change reduce CPU time for the hot decision path in compute logic?".

Copy `benchmarks/sample_benchmark.cpp` into a module-specific benchmark file, keep setup outside the measured loop, and isolate one workload shape per benchmark. Register the new target in `benchmarks/CMakeLists.txt`, then run `make build` and execute the benchmark binary (for example `./build/debug/benchmarks/sample_benchmark`) to compare `CPU` time across baseline vs changed code.

Benchmark results are comparative, and they are contextual. If the machine is under load, if you changed build type, or if you changed inputs, your numbers won’t be comparable. When you include benchmark evidence in a PR, include the benchmark name, the input shape, and the build profile so another developer can reproduce the comparison.

## Chapter 12: Clean vs deepclean

`make clean` and `make deepclean` are different tools.

`make clean` runs target-level clean operations for configured build directories (when present) and removes raw logs for the active branch lane (`logs/make/<branch>/raw_logs/`).

`make deepclean` removes:
- `build/`
- `logs/make/<branch>/`
- `warnings/`

Use `clean` for normal iteration, and `deepclean` when you want to reset generated artifacts and reports.

## Chapter 13: Extending the system correctly

When you add a new module, the most important design decision is where the logic lives. In a C project it is tempting to put behavior directly in `main.c`, but that makes the code hard to test and hard to reuse. In this build system we treat `main` as an entrypoint and we treat most behavior as a library.

In CMake terms, “define a library target for reusable logic” means: create an `add_library(...)` target that compiles the `.c` files containing the module’s actual behavior, exports the module’s include path, and links the dependencies that the behavior needs. Then every executable, test, or benchmark that uses that behavior links the library target instead of re-compiling the same sources or copying code around.

There are three practical benefits:

First, it makes linkage explicit. If the reusable logic needs `pthread` or `curl` or `cJSON`, you express that once on the library target and consumers inherit the correct link flags.

Second, it makes testing and benchmarking natural. A unit test can link the library and call functions directly, without having to run a long-lived daemon or parse syslog output just to verify a calculation.

Third, it keeps compile settings consistent. Include paths, compile definitions, sanitizer flags, and clang-tidy scope are applied to targets; when logic is a target, it automatically stays within the same policies as the rest of the repo.

You can see the pattern in this repository already. The compute module is split into a library and an executable:

- `sunspots_compute` is a library target that compiles `compute.c` and exports its include directory.
- `sunspots_compute_manager` is the process entrypoint that links `sunspots_compute` and adds runtime concerns (threads, signals, IO).

The frontend is similar:

- `sunspots_frontend_core` is the reusable server logic.
- `sunspots_frontend` is the `main` that starts the process.

In plain terms: if a function could be called from a test, it probably belongs in a library target. If a file’s job is “parse argv, start threads, run loop, shut down”, it probably belongs in an executable target.

When you add a new module, follow this stable pattern:

1. Add or update `src/<component>/CMakeLists.txt`.
2. Create `add_library(sunspots_<component> ...)` for the reusable logic (no `main`).
3. Create `add_executable(sunspots_<component>_<role> ...)` for the entrypoint.
4. `target_link_libraries(...)` on the executable to link the library target and any extra runtime dependencies.
5. Add tests by copying the structure of an existing file in `tests/unit/` and linking your library target.
6. If performance is a risk, add benchmarks by copying `benchmarks/sample_benchmark.cpp` and linking your library target.

If you follow this pattern, the build system stays readable, your modules stay testable, and quality tools stay usable.

## Chapter 14: A practical self-study path

If you want one concrete order to learn the repository, use this:

1. `README.md`
2. `Makefile`
3. `CMakeLists.txt`
4. `cmake/SunspotsOptions.cmake`
5. `cmake/SunspotsDependencies.cmake`
6. `src/CMakeLists.txt`
7. one component `src/<component>/CMakeLists.txt`
8. `tests/CMakeLists.txt`
9. one test file in `tests/unit/`
10. `benchmarks/CMakeLists.txt`
11. `benchmarks/sample_benchmark.cpp`

Then run:

```bash
make deepclean
make
make run-tests
make warnings
```

After that sequence, you will have exercised the main development loop.
