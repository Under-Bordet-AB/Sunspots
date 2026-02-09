# Build System Manual

| Attribute | Value |
| :--- | :--- |
| **System** | CMake + Ninja |
| **Workflow** | `make` wrapper (optional) |
| **Outputs** | repo-local `bin/` |
| **Build Dirs** | `build/debug`, `build/release`, `build/dev` |

## Overview

Sunspots is a multi-program repository:

- one daemon/supervisor
- multiple worker programs (fetchers, compute, etc)
- shared libraries (SDK and vendored libraries)

That means the build must produce **multiple executables**, each with its own `main()`.

The build system is designed around a professional C pattern:

1. Build shared code into libraries once.
2. Link many executables against those libraries.

## Quick Start

Build everything (debug + sanitizers):

```bash
make
```

Or explicitly:

```bash
make debug
make release
```

Run the daemon:

```bash
make run-daemon
```

If you prefer calling CMake directly (same result):

```bash
cmake --preset debug
cmake --build --preset debug
```

## Build Presets (What They Mean)

Presets live in `CMakePresets.json`.

- `debug`: Debug build with AddressSanitizer + UndefinedBehaviorSanitizer enabled.
  - Best for day-to-day development; catches memory bugs early.
- `dev`: Debug build without sanitizers.
  - Useful when you need speed or when sanitizers get in the way.
- `release`: Optimized build.
  - Use when you want realistic performance measurements.

## Output Layout

Executables are written to the repo-local `bin/` directory. Example:

- `bin/sunspots_daemon`
- `bin/fetch_manager`
- `bin/fetch_openmeteo` (only if `libcurl` is available)

Object files and CMake state live under `build/*`. You can delete `build/` at any time.

## Optional Dependencies

Some binaries depend on external system libraries.

### libcurl

Fetch workers use `libcurl` via `src/libs/curly.c`.

If CMake cannot find `libcurl`, the build will still succeed but will skip the fetch-worker executables (`fetch_openmeteo`, `fetch_elprisjustnu`). Install `libcurl` development headers and re-run the preset to enable them.

## Adding a New Program (How-To)

When you add a new program with its own `main()`:

1. Create the new `*.c` file under an appropriate module directory.
2. Add an `add_executable(...)` entry in the root `CMakeLists.txt`.
3. Link it against the libraries it uses (`ss_sdk`, `cjson`, `curly`, `Threads::Threads`, etc).

The rule of thumb is:

- shared code belongs in a library target
- `main()` belongs only in an executable target

This avoids the classic multi-main linker problems and keeps incremental builds fast.

## Make Wrapper (Why It Exists)

The root `Makefile` is intentionally tiny. It exists so the team can keep a simple workflow:

- `make debug`
- `make release`
- `make run-daemon`

Under the hood, it calls CMake presets. CMake is the source of truth.

