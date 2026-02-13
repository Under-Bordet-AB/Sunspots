# Sunspots Build and Quality Document

This text is written for C developers who are new to modern build orchestration and quality tooling.

You already know how programs are written in C. What you need now is a clean mental model for how a multi-program C/C++ repository is built, tested, and benchmarked in a consistent way.

This Document is meant to be read from top to bottom. If you follow it in order and run the commands shown, you should understand the full local workflow in this repository.

## Chapter 1: What problem are we solving?

A small C project can survive with one Makefile and a handful of source files. Sunspots is not that shape.

Sunspots has multiple executables. It has reusable logic. It has external dependencies. It needs testing. It needs a workflow that every developer can run the same way.

The central problem is not "how do I compile one `.c` file?". The central problem is "how do we keep dozens of build and quality steps predictable while the project evolves?"

That is why this project uses CMake as the source of truth and a root `Makefile` as a convenience interface.

CMake defines what exists and how it links.
The root `Makefile` defines a friendly command vocabulary for humans.

This split is important. It keeps architecture in one place and day-to-day commands easy.

## Chapter 2: The two-layer model

Think of the repository as two layers.

Layer 1 is declaration.
This is the CMake layer. It declares targets, dependencies, compile flags, tool options, and test registration.

Layer 2 is usage.
This is the `make` layer at repository root. It translates common developer actions into the right CMake/CTest commands.

If there is ever a conflict, CMake wins. The wrapper should not invent separate behavior.

Open these files and read them in this order:
- `CMakeLists.txt`
- `cmake/SunspotsOptions.cmake`
- `cmake/SunspotsDependencies.cmake`
- `src/CMakeLists.txt`
- `Makefile`

Now run:

```bash
make
```

When this command works, the two-layer model is functioning.

## Chapter 3: How the project is physically organized

You can understand most of the repository by reading `src/CMakeLists.txt` and then one component at a time.

Each component directory gets its own `CMakeLists.txt`. That local file answers three questions:

What does this component build?
How does it compile?
What does it link to?

The `src/libs/` area contains reusable building blocks. Components can link them instead of copying implementation code.

The important design choice is that process entrypoints are separate from reusable logic.

A process entrypoint lives in an executable target. It is usually where argument parsing, process lifecycle, and OS interaction begin.

Reusable logic lives in library targets. That is what tests and benchmarks should call directly.

This separation makes the code easier to reason about and easier to verify.

## Chapter 4: What happens when you run `make`?

The default target is `build`.

`make` runs configuration first, then build.

Configuration means CMake reads every `CMakeLists.txt` file, resolves dependencies, and generates build files in `build/debug`.

Build means your compiler and linker execute the generated plan.

So the real sequence is:
1. configure (`cmake -S . -B build/debug ...`)
2. compile/link (`cmake --build build/debug`)

When configuration prints lines about fetching GoogleTest or Google Benchmark, that means local system packages were not found and CMake is using fallback source downloads. That is expected behavior in this setup.

## Chapter 5: Standards, warnings, and sanitizer behavior

This project targets C99 and C++11.

That choice is explicit in top-level CMake settings. It is not left to compiler defaults.

Warnings are configured to be useful but not extreme. The goal is to catch obvious mistakes while keeping development practical in an evolving codebase.

In Debug mode, sanitizers are enabled by default.

AddressSanitizer helps catch memory errors such as out-of-bounds access and use-after-free.

UndefinedBehaviorSanitizer helps catch invalid operations that can look harmless but are semantically broken.

This default is intentionally aggressive. It moves memory and UB feedback to the moment developers are actively coding.

## Chapter 6: Dependency strategy without mystery

Dependencies are resolved in two stages.

First, CMake tries system packages.

If a package is not available, CMake can fetch sources for selected libraries used in testing and benchmarking.

This gives two benefits.

On configured developer machines, builds are fast and use system-provided libraries.

On new or minimal environments, the project can still bootstrap itself.

In this repository, that pattern is applied to GoogleTest and Google Benchmark.

## Chapter 7: Testing in this repository

At the moment, there is a deliberately named sample test file:
`tests/unit/sample_test.cpp`

The name is important. It is not pretending to be complete coverage. It is a reference implementation of structure and wiring.

How tests are connected:

CMake builds a test executable.
CTest registers that executable as a runnable test.
The root `make test` command invokes CTest.

Run:

```bash
make test
```

Then open:
- `tests/CMakeLists.txt`
- `tests/unit/sample_test.cpp`

Read the sample test as a pattern:
- arrange input and state
- call one function
- assert result and output state

That is the core form every module owner should replicate for real module tests.

## Chapter 8: Why C tests are written from C++ files here

You may ask why a C repository uses GoogleTest (a C++ framework).

The reason is consistency and ecosystem maturity.

For C code, the bridge is simple:
include C headers inside `extern "C"` blocks.

This allows one test framework for both C and C++ targets while still testing C APIs directly.

The sample test shows exactly how this bridge looks.

## Chapter 9: Benchmarking and what it is not

There is also a deliberately named sample benchmark:
`benchmarks/sample_benchmark.cpp`

Again, the name communicates intent. It is a teaching reference, not a complete performance suite.

Run:

```bash
make bench
```

This command runs the benchmark executable directly.

A benchmark is not a correctness test. It is a measurement tool.

Correctness answers "is behavior right?"

Benchmarking answers "how expensive is this path?"

Both matter, but they solve different problems.

To use benchmarking well, you need to understand what the framework is actually doing under the hood.
Google Benchmark repeatedly calls a function body many times and measures how long that body takes.
You write a benchmark function that accepts `benchmark::State& state`, and then you put the code you want to measure inside:

`for (auto _ : state) { /* measured code */ }`

That loop is the core contract. Google Benchmark controls how many iterations run. You do not manually choose a fixed count in code for normal use. The framework increases and adjusts iteration count so it can produce stable timing statistics with enough samples.

In this repository, `make bench` runs `build/debug/benchmarks/sample_benchmark`.
When it runs, you see two kinds of output. First, machine context: timestamp, CPU description, cache sizes, and load average. Second, benchmark rows.

A typical row looks like this conceptually:

`BM_SampleCalculateSimple   <Time>   <CPU>   <Iterations>`

Here is what those columns mean:

`Time` is wall-clock time per iteration. This includes scheduler effects and system noise.
`CPU` is CPU time per iteration, which is usually the better signal for algorithmic cost on a shared machine.
`Iterations` is how many loop iterations the framework executed to estimate a stable result.

You may also see a warning that the benchmark library was built in Debug mode. That warning matters. Debug builds are useful for learning workflow, but they are not ideal for final performance claims because optimization is lower and instrumentation may exist. For serious measurements, compare Release-like builds under stable machine conditions.

Now the important practical question: what inputs should a benchmark use?
The answer is that benchmark inputs must represent realistic workload shapes, not random toy values and not full system IO.
For a compute function, inputs should cover the typical value ranges and branch paths you expect in production. If one branch is common and another is rare but expensive, you usually want separate benchmark cases for both, so you can see each cost clearly instead of averaging everything into one number.

Another key point is scope. A microbenchmark should isolate computation. Network calls, file IO, sleep calls, logging, and process orchestration usually dominate timing and hide the cost of the code path you are trying to inspect. Keep those outside the measured loop unless your explicit goal is end-to-end latency.

You should also understand `benchmark::DoNotOptimize(...)` and `benchmark::ClobberMemory()`, which appear in the sample benchmark.
They are there to reduce invalid optimizations by the compiler that could remove or simplify work unrealistically during benchmarking. Without them, you can get deceptively fast numbers that do not correspond to real execution behavior.

As your benchmarks mature, think of each benchmark as a performance test case with a clear question.
Examples of good questions are:
"How much does this parser cost per input record?"
"Did this new branch-reduction change reduce CPU time for the hot path?"
"Is memory-copy strategy A cheaper than strategy B for our typical payload size?"

When you can state that question clearly, choose representative inputs, and read `CPU` and `Iterations` with context, benchmarking becomes a reliable engineering tool rather than a vanity number generator.

## Chapter 10: Mini Tutorial - Writing a Professional Test

This chapter is a practical walkthrough, not theory. You will write one test the way a production team should write it: clear intent, deterministic setup, and behavior-focused assertions.

The tutorial goal is to add a test for one real behavior, not to maximize lines covered.

Start by choosing one function with stable inputs and outputs. In this repository, a suitable example is `calculate_simple` in `src/compute/compute.c`.

Before writing any test code, define the behavior in plain language. For example:
"When solar availability is high, the decision should prefer solar and avoid buying electricity."

That sentence becomes the contract your test enforces.

Now create a new test file, for example:
`tests/unit/compute_decision_test.cpp`

Follow the same bridge style used in `tests/unit/sample_test.cpp`, including C headers through `extern "C"`.

Write the test using Arrange / Act / Assert:

Arrange means create one explicit input state with values that force the branch you care about.
Act means call exactly one function under test.
Assert means verify both status/return value and the output fields that represent business behavior.

A professional test here avoids three common mistakes.

First, it does not rely on random inputs. Randomness makes failures hard to reproduce.
Second, it does not test multiple unrelated behaviors in one test case. That makes failures ambiguous.
Third, it does not assert trivial internals that are likely to change during refactoring.

After writing the test code, register it in `tests/CMakeLists.txt` as a new executable and CTest entry. Give it a descriptive test name and labels.

Then run:

```bash
make test
```

If the test fails, do not immediately change assertions. Read the failure and decide whether the code is wrong or the expected behavior sentence was wrong. Keep that distinction strict.

Finally, do a review pass on naming.
A professional test name should read like a behavior statement, not like an implementation detail. If someone can understand intent from the test name alone, your test is maintainable.

## Chapter 11: Mini Tutorial - Running a Professional Performance Test

Now you will run a benchmark workflow with the same discipline: one clear performance question, controlled inputs, and careful interpretation.

Start with a question, not a command. Example:
"Does this change reduce CPU time for the hot decision path in compute logic?"

Without that question, benchmark numbers are just noise.

Use `benchmarks/sample_benchmark.cpp` as the template and create a benchmark file for your module, for example:
`benchmarks/compute_decision_benchmark.cpp`

The measured part must stay inside the `for (auto _ : state)` loop.
Put setup code outside the loop when possible so you do not benchmark initialization overhead by accident.

Choose inputs that represent realistic module usage.
If your function has fast and slow branches, define separate benchmark functions for each branch. Mixing all paths into one benchmark hides useful information.

Register the benchmark target in `benchmarks/CMakeLists.txt`, then run:

```bash
make bench
```

Read output in this order:

First check environment context (CPU, load average). If the machine is heavily loaded, be careful with conclusions.
Then compare `CPU` time across runs; this is usually the most useful per-iteration signal.
Use `Iterations` as a confidence clue: very low iteration counts often mean unstable measurements.

Professional performance testing is comparative.
Run once on baseline code, run again on changed code, and compare the same benchmark with the same input shape. Keep notes in your PR so reviewers can verify what changed and why it matters.

Also treat benchmark regressions as hypotheses, not immediate truth. Re-run to confirm, and make sure no unrelated environment change explains the difference.

When done correctly, a benchmark result should let you answer one sentence clearly:
"For this workload shape, this code path became faster/slower by an amount we can explain."

## Chapter 12: Clean vs deepclean

`make clean` and `make deepclean` are intentionally different.

`make clean` performs target-level clean operations for configured build directories when present.
It also prints a one-line status message so the user can see what happened.

`make deepclean` removes the full `build/` tree and prints a one-line status message.

Use clean for normal iteration.
Use deepclean when you need a full rebuild from scratch.

## Chapter 13: How to extend this project correctly

Suppose you add a new module.

The stable pattern is:

Create or update the component `CMakeLists.txt` in `src/<component>/`.
Define one library target for reusable logic.
Define one executable target for process entrypoint.
Link required internal and external dependencies.
Add one or more real tests by copying the structure of `sample_test.cpp`.
If performance-sensitive, add benchmarks by copying the structure of `sample_benchmark.cpp`.

This is the path that keeps code understandable and quality tooling usable.

## Chapter 14: Practical reading path through the codebase

If you want one concrete order for a self-study pass, use this:

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

Then run this sequence:

```bash
make deepclean
make
make test
make bench
```

After this, you should have seen every major part of the development lifecycle in one pass.

## Chapter 15: What "done" means for your understanding

You understand the system when you can explain all of the following without guessing:

Where a target is declared.
Where dependencies are linked.
Where warning/sanitizer policy is defined.
How a test executable becomes a CTest test.
Why benchmark code is separate from unit tests.
When to use clean versus deepclean.

If you can answer those questions, you are no longer using the build and quality system as a black box.
You can maintain it safely.

## Chapter 16: Final note for module owners

The current sample test and sample benchmark are intentionally minimal references.

They are designed to teach structure, not to claim broad verification.

As you take ownership of modules, replace "sample mindset" with module-specific behavior checks and benchmark scenarios tied to real performance risks.

That is where project quality becomes real.
