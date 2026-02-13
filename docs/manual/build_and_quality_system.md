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

Layer 2 is the usage layer: the root `Makefile` runs CMake and CTest with consistent arguments so the team can type short commands like `make build` or `make test`.

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

The default `make` goal is `build`. That means `make` performs a configure step (CMake generates build files) and then a build step (your compiler and linker run the plan).

Conceptually, it is:

1. Configure: `cmake -S . -B build/debug ...`
2. Build: `cmake --build build/debug --parallel`

The configure step may print messages about fetching GoogleTest or Google Benchmark. That simply means those packages were not found via the system package manager, so CMake is bootstrapping them using `FetchContent`. This is expected in this project.

## Chapter 5: Standards, warnings, and sanitizers

Sunspots targets C99 and C++11. This is not left to compiler defaults.

Warnings are enabled to catch common mistakes without turning the build into a constant fight. The goal is to keep warnings meaningful and actionable, not to enforce perfection at the cost of velocity.

Debug builds enable sanitizers by default. Sanitizers are not a replacement for tests, but they are excellent at making classes of memory and undefined-behavior defects visible during everyday development. When a sanitizer report happens, treat it as a correctness bug, not as a "tool issue".

## Chapter 6: Dependencies without mystery

External dependencies are resolved with a simple rule:

1. Prefer system packages when available.
2. If a developer machine (or CI) does not have them, bootstrap a known version for test/benchmark dependencies.

In practice, `cmake/SunspotsDependencies.cmake` is where to look. It contains all the "how we find or fetch" logic.

This matters because it makes onboarding simpler: a new developer can build without spending half a day learning which packages are missing. It also matters because it makes CI reproducible: the build graph always has a path forward.

## Chapter 7: Testing in this repository

Tests are built as normal executables, and then registered with CTest. The wrapper target `make test` calls CTest for the current build directory.

A deliberately named placeholder test exists as a template:
- `tests/unit/sample_test.cpp`

It is not meant as finished coverage. Its job is to show how to write a test that is readable and stable.

Run:

```bash
make test
```

Then open:
- `tests/CMakeLists.txt`
- `tests/unit/sample_test.cpp`

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
make bench
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
make test
```

If it fails, don’t "fix" the test immediately. Decide whether the code is wrong or the behavior sentence was wrong. The value of tests is that they force you to make that decision explicitly.

## Chapter 11: Mini tutorial - running a professional performance test

Professional performance work starts with a question. Example: "Does this change reduce CPU time for the hot decision path in compute logic?".

Copy `benchmarks/sample_benchmark.cpp` into a module-specific benchmark file, keep setup outside the measured loop, and isolate one workload shape per benchmark. Register the new target in `benchmarks/CMakeLists.txt`, then run `make bench` and compare `CPU` time across baseline vs changed code.

Benchmark results are comparative, and they are contextual. If the machine is under load, if you changed build type, or if you changed inputs, your numbers won’t be comparable. When you include benchmark evidence in a PR, include the benchmark name, the input shape, and the build profile so another developer can reproduce the comparison.

## Chapter 12: Clean vs deepclean

`make clean` and `make deepclean` are different tools.

`make clean` runs target-level clean operations for configured build directories (when present). `make deepclean` removes the entire `build/` tree. Use `clean` for normal iteration, and use `deepclean` when you need a full rebuild from scratch.

## Chapter 13: Extending the system correctly

When you add a new module, follow the stable pattern:

- add or update `src/<component>/CMakeLists.txt`
- define a library target for reusable logic
- define an executable target for the entrypoint
- link explicit dependencies
- add real tests by copying the structure of `tests/unit/sample_test.cpp`
- add benchmarks by copying the structure of `benchmarks/sample_benchmark.cpp` when performance is a risk

If you follow this pattern, the build system stays readable and the quality tools stay usable.

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
9. `tests/unit/sample_test.cpp`
10. `benchmarks/CMakeLists.txt`
11. `benchmarks/sample_benchmark.cpp`

Then run:

```bash
make deepclean
make
make test
make bench
```

After that sequence, you will have exercised the main development loop.

## Chapter 15: What “done” means

You understand the system when you can explain these without guessing:

- where targets are declared
- where dependencies are linked
- where warning/sanitizer policy is defined
- how a test executable becomes a CTest test
- why benchmarks are separate from tests
- when to use clean vs deepclean

That is the point where the build and quality system stops being a black box.
