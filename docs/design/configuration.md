# Configuration Module Design

> **Status**: DRAFT
> **Pattern**: Design Pattern 6 (Configuration Object)

## 1. Goal
Implement a centralized Configuration Module following **Design Pattern 6** from the project standards. This module will load a monolithic configuration file (JSON) and providing type-safe accessors to "slice" this configuration into module-specific C structs.

## 2. Architecture

### 2.1 Component Diagram
```
+-----------+    1. Load    +---------------+
| Main Loop | ------------> | Config Module |
+-----------+               +---------------+
      |                            |
      | 4. Init                    | 2. Parse
      v                            v
+--------+   3. Slice      +----------------+
| Server | <. . . . . . .  | JSON Lib       |
+--------+                 | (cJSON)        |
      ^                    +----------------+
      | 3. Slice
+----------+
| Database |
+----------+
```

### 2.2 Decisions
*   **JSON Parser**: Use **cJSON**.
    *   **Strategy**: **Vendor** (Copy-paste).
    *   **Reason**: cJSON is only two files (`cJSON.h`, `cJSON.c`). Vendoring minimizes build complexity (`git submodule` is overkill) and ensures we only add the essential files, keeping the file count low.
*   **Encapsulation**: The underlying JSON object will be hidden behind an opaque `config` handle.
*   **Type Safety**: The module will expose typed accessors (`config_get_int`, `config_get_string`) rather than exposing raw JSON nodes.

## 3. Public API Design

The API adheres to the project's "Opaque Pointer" and "Lifecycle" standards.

```c
// src/config/config.h

typedef struct config config;

// Lifecycle
int  config_load(const char* path, config** out);
void config_destroy(config** cfg);

// Slicing
int config_get_section(const config* root, const char* key, config** out_section);

// Accessors
// Returns 0 on success, -ENOENT if key missing, -EINVAL if type mismatch
int config_get_int(const config* cfg, const char* key, int* out);
int config_get_bool(const config* cfg, const char* key, bool* out);
int config_get_string(const config* cfg, const char* key, char* buf, size_t size);

// Support for dot-notation keys: "server.port"
```

## 4. Usage Example

```c
// main.c

int main(void) {
    // 1. Load Global Config
    config* global_cfg = NULL;
    if (config_load("config.json", &global_cfg) != 0) {
        return EXIT_FAILURE;
    }

    // 2. Prepare Module Configs
    // A. Server
    config* srv_json = NULL;
    if (config_get_section(global_cfg, "server", &srv_json) == 0) {
        server_config s_cfg = {0};
        config_get_int(srv_json, "port", &s_cfg.port);
        // ... get other server fields ...
        
        server_init(&srv, &s_cfg);
        config_destroy(&srv_json); // Free the subsection handle (not the data)
    }

    // B. Database
    config* db_json = NULL;
    if (config_get_section(global_cfg, "database", &db_json) == 0) {
        // ... extract db fields ...
        config_destroy(&db_json);
    }

    // 4. Cleanup
    config_destroy(&global_cfg); // Config no longer needed after init
    return EXIT_SUCCESS;
}
```

## 5. Implementation Plan
1.  **Add cJSON**: Import `cJSON.c` and `cJSON.h` into `src/libs/json/`.
2.  **Scaffold Config**: Create `src/config/config.c` and `src/config/config.h`.
3.  **Implement Load**: Write `config_load` to read file => parse string => cJSON_Parse.
4.  **Implement Accessors**: Write wrappers to traverse cJSON objects using dot-notation splitting.
## 6. Design Discussion: Singleton vs Orchestrator

We debated two approaches for managing the configuration lifecycle.

### User Constraint
> "We don't want to have to include the config [in modules], so only the main file gets `config.h`."

This constraint effectively rules out a global singleton that modules access directly (e.g., `config_get_global()`). If modules cannot see `config.h`, they cannot call config functions. Therefore, `main` (the orchestrator) **must** be responsible for extracting values and passing them to modules.

### Option A: Explicit Object (Recommended)
*   **Mechanism**: `config_load` returns a `config*` handle. `main` passes this handle to `config_get_section` and `config_get_int`.
*   **Pros**:
    *   **No Global State**: Easy to test (can load `test_config.json` in parallel with real config).
    *   **Thread Safety**: Inherently better since there's no shared global mutable state.
    *   **Clarity**: Data flow is explicit.
*   **Cons**: `main` must hold the variable `config* cfg`.

### Option B: Implicit Global Singleton (Alternative)
*   **Mechanism**: `config_init()` loads a hardcoded path (`./config.json`) into a hidden static variable. Accessors like `config_get_int` use this internal static variable if a NULL handle is passed (or don't take a handle at all).
*   **Pros**:
    *   `main` doesn't need to store the `root` pointer.
*   **Cons**:
    *   Hidden state.
    *   Harder to unit test (must "reset" the global for each test).
    *   Hardcoded path reduces flexibility.

**Decision**: We proceed with **Option A (Explicit Object)** because it aligns best with the "Orchestrator" pattern required by the dependency constraint. However, we can add a helper `config_load_defaults(&cfg)` that hardcodes the path to `config/config.json` to simplify `main`.

