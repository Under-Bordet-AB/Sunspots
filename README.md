# Sunspots Core

Sunspots is a high-performance C99-based server designed for efficiency and reliability. This repository contains the core logic, daemonization support, and a robust configuration management system.

## 🚀 Getting Started

### Prerequisites
- **GCC**: C99 compliant compiler.
- **Make**: Build automation tool.
- **Clang-Tools**: `clang-format` and `clang-tidy` for code quality.
- **Valgrind**: For memory leak detection (optional).

### Quick Start
To format, build, and run the server in one command:
```bash
make run
```

To see a celebration after your hard work:
```bash
make fireworks
```

## 🛠️ Build System

The build system is designed for modern C development, enforcing strict formatting and linting rules during iteration.

### Available Targets

| Target | Description |
| :--- | :--- |
| `make all` | **Default Pipeline**: Runs Format -> Lint -> Compile -> Test -> Link. |
| `make debug` | Builds with `-g`, `-O0`, and AddressSanitizer (ASAN). |
| `make release` | Builds with `-O2` optimizations and disables debug macros. |
| `make test` | Compiles and executes the unit test suite with colorful summaries. |
| `make lint` | Runs `clang-tidy` against the source code (excluding libs). |
| `make format` | Automatically formats all `.c` and `.h` files using `clang-format`. |
| `make clean` | Removes all build artifacts (`build/` directory). |
| `make run` | Formats, builds (debug), and executes the server. |
| `make valgrind`| Runs the server under `valgrind` for deep memory analysis. |
| `make fireworks`| 🎆 Displays a colorful ASCII celebration in your terminal. |

### Build Pipeline
When running `make all`, the following steps are executed:
1.  **Formatting**: Every build starts with a code style check/auto-fix.
2.  **Linting**: Static analysis via `clang-tidy` to catch potential bugs early.
3.  **Compilation**: Parallel-safe compilation of all translation units.
4.  **Unit Testing**: Execution of all discovered tests in `tests/`.
5.  **Linking**: Final binary creation in `build/bin/server`.

## 📁 Project Structure
- `src/`: Core implementation.
- `src/libs/`: Third-party libraries (e.g., `cJSON`).
- `tests/`: Unit and integration tests.
- `docs/`: Coding standards and contribution guides.

---
*Maintained by the Sunspots Team.*
