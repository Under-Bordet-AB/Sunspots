# Configuration Module Manual

| Attribute | Value |
| :--- | :--- |
| **Module** | `src/config` |
| **Status** | Design Phase |
| **Pattern** | Composable Configuration & Subtree Slicing |
| **Dependencies** | `cJSON` (Vendored), `jj_log` (Submodule) |

## Overview

The Configuration Module provides a **Composable** and **Zero-Boilerplate** way to manage application settings. It uses a **Subtree-Based** access pattern, meaning modules "borrow" sections of the configuration without managing memory.

**Key Features:**
*   **Zero Boilerplate**: No error checking loops for standard retrieval.
*   **Safe Defaults**: Missing keys or sections harmlessly return default values.
*   **Subtree Semantics**: Subsections are pointers into the main tree, not copies. No `free()` needed for modules.
*   **Dot Notation**: Access deep values directly (e.g., `"modules.server.port"`).
*   **CLI Overrides**: Override any setting via `--key value` command-line arguments.

## Quick Start

### 1. The Configuration File
`config/sunspots.json`:
```json
{
  "system": {
    "version": "1.0",
    "name": "Sunspots",
    "debug": true
  },
  "modules": {
    "core": {},
    "server": {},
    "database": {},
    "compute": {},
    "fetch": {},
    "transform": {}
  }
}
```

### 2. Main Initialization (The "Subtree" Pattern)
In `main.c`, you load the root implementation. Modules verify nothing; they just accept a handle.

```c
#include "config/config.h"

int main(void) {
    // 1. Owning Root (The only thing you free)
    config* cfg = config_create();
    config_load_file(cfg, "config/sunspots.json");
    config_load_env(cfg); // second highest priority
    config_load_args(cfg, argc, argv); // highest priority

    // 2. Pass "Subtrees" (Borrowed Pointers)
    // If "modules.server" is missing, returns NULL. 
    // Modules handle NULL gracefully.
    server_init(config_get_subtree(cfg, "modules.server"));
    database_init(config_get_subtree(cfg, "modules.database"));
    compute_init(config_get_subtree(cfg, "modules.compute"));

    // 3. Cleanup
    config_destroy(cfg);
    return 0;
}
```

### 3. Module Usage (Defaults-First)
Modules use `_or` variants to specify defaults. If the config handle is `NULL` (e.g., section missing), the default is returned immediately.

```c
void server_init(const config* cfg) {
    // If cfg is NULL, or key missing -> use 8080
    int port = config_get_int_or(cfg, "port", 8080);
    
    // Safely verify specific requirements if needed
    if (!cfg) {
        log_warn("No config provided, using defaults");
    }
}
```

## Configuration Precedence

The precedence is determined by the **order of function calls** in `main.c`. Later calls overwrite values from earlier ones. The recommended standard order is:

1.  **Command Line Arguments** (`--key value`) - Highest Priority (Called last)
2.  **Environment Variables** (`SUNSPOTS_*`)
3.  **JSON Configuration File** (`config/sunspots.json`)

## Command Line Arguments

The module supports a standard `--key value` format. 

*   **Dot Notation**: Support for nested keys (e.g., `--server.port 9090`).
*   **Auto-Typing**:
    *   `true` / `false` -> Boolean
    *   Digits -> Number
    *   Other -> String
*   **Strings**: Quotes are handled by the shell (e.g., `--name "Sunspots Server"`).

**Example:**
```bash
./server --port 9090 --server.ssl.enabled true --system.name "Custom Name"
```

## API Reference

### Lifecycle

*   `config* config_create(void)`
    *   Allocates the root configuration.
*   `void config_destroy(config* cfg)`
    *   Frees the root and ALL its children.
*   `int config_load_file(config* cfg, const char* path)`
*   `int config_load_env(config* cfg)`
*   `int config_load_args(config* cfg, int argc, char** argv)`

### Navigation (Subtrees)

*   `const config* config_get_subtree(const config* root, const char* path)`
    *   Returns a **borrowed pointer** to a subsection.
    *   Supports dot notation: `"modules.server"`.
    *   Returns `NULL` if not found.
    *   **NEVER** call `config_destroy` on this pointer.

### Accessors (Safe)

All accessors accept `NULL` as the `cfg` parameter (returning the default).

*   `int config_get_int_or(const config* cfg, const char* key, int default_val)`
*   `bool config_get_bool_or(const config* cfg, const char* key, bool default_val)`
*   `const char* config_get_string_or(const config* cfg, const char* key, const char* default_val)`
    *   Returns a pointer to the internal string (valid as long as `root` is valid).

### Accessors (Strict)

Used when a value is **mandatory**.

*   `int config_get_int(const config* cfg, const char* key, int* out)`
    *   Returns `-ENOENT` if missing.
