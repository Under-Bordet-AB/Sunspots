# Atomic File Manual

**Library:** `atomic_file_rw.h`
**Purpose:** Simple, process-safe data exchange using a shared JSON file.

## 1. Overview

This library provides a mechanism for multiple programs ("Writers") to append data to a shared log file, and for other programs ("Readers") to read the current state of that log.

*   **Mechanism:** Advisory File Locking (`flock`) on a standard file.
*   **Format:** NDJSON (Newline Delimited JSON).
*   **Safety:** Guarantees atomic writes (no data interleaving) and consistent reads.
*   **Performance:** High-performance in uncontended cases (Linux Futexes), blocks (sleeps) when contended.

---

## 2. Installation
This is a **Single Header Library** that depends on `cJSON`.

### Requirements
1.  Copy `atomic_file_rw.h` to your source tree.
2.  Copy `cJSON.h` and `cJSON.c` to your source tree (or link against system cJSON).

### Integration
In **ONE** source file (e.g., `main.c` or `ipc_impl.c`), do this:

```c
#define ATOMIC_FILE_RW_IMPLEMENTATION
#include "atomic_file_rw.h"
```

In all other files, just include it:

```c
#include "atomic_file_rw.h"
```

### Compilation
You must compile and link `cJSON`.

```bash
gcc -o my_app main.c cJSON.c -lpthread
```

---

## 3. Data Format & Schema

All data is written to a hardcoded file path: `./database.jsonl` (relative to execution directory).

The library **enforces** a strict JSON schema for every line. You provide the `Source`, `Type`, and `Data`, and the library adds the `Timestamp`.

**Schema:**
```json
{
  "timestamp": 1706531200,    // Unix Epoch (Seconds), auto-generated
  "source": "API_smhi",       // Use AF_SOURCE_* macros
  "type": "temperature",      // Use AF_TYPE_* macros
  "data": "23.5"              // string
}
```

### Standard Constants
To avoid typos, use the defined macros in your code:

**Sources:**
*   `AF_SOURCE_API_SMHI` ("API_smhi")
*   `AF_SOURCE_SENSOR` ("Sensor_Network")
*   `AF_SOURCE_CALCULATOR` ("Calculator_Svc")
*   `AF_SOURCE_SYSTEM` ("System_Log")

**Types:**
*   `AF_TYPE_TEMPERATURE` ("temperature")
*   `AF_TYPE_HUMIDITY` ("humidity")
*   `AF_TYPE_POWER` ("power_usage")
*   `AF_TYPE_LOG` ("log_entry")
*   `AF_TYPE_ERROR` ("error_report")

---

## 4. API Reference

### Writers: `af_save`

Use this to append a new event to the log.

```c
/**
 * Saves a structured event to the file.
 * BLOCKING: Sleeps if another process is currently writing.
 *
 * @param source  Origin (e.g., "API_Weather", "Sensor_X")
 * @param type    Category (e.g., "temperature", "boot_log")
 * @param data    The payload string.
 * @return        0 on success, -1 on error.
 */
int af_save(const char *source, const char *type, const char *data);
```

**Example:**
```c
if (af_save("WeatherAPI", "temperature", "23.5") != 0) {
    perror("Write failed");
}
```

### Readers: `af_read`

Use this to read the **entire** content of the log file at once.

```c
/**
 * Reads the ENTIRE file content.
 * BLOCKING: Sleeps if a Writer is currently writing.
 *
 * @param out_size  [Optional] Pointer to store the size of read data.
 * @return          Malloc'd buffer containing the file data. 
 *                  You MUST free() this buffer. Returns NULL on error.
 */
char *af_read(size_t *out_size);
```

**Example:**
```c
size_t size = 0;
char *content = af_read(&size);

if (content) {
    printf("Read %zu bytes:\n%s\n", size, content);
    free(content); // IMPORTANT
}
```

---

## 5. Concurrency Model (Blocking)

This library uses **Blocking synchronization**.

### How it works
1.  **Writer:** Tries to acquire `LOCK_EX`. If the file is busy (Writer or Reader active), the calling thread **Sleeps** (0% CPU) until the lock is free.
2.  **Reader:** Tries to acquire `LOCK_SH`. If a Writer is active, the calling thread **Sleeps**. Multiple Readers can read simultaneously.

### Best Practices
*   **Don't block the UI:** If your application has a GUI or high-frequency loop, running `af_save` directly might cause "stutters" if the disk is busy. Move writes to a background thread if this happens.
*   **Short Reads:** `af_read` locks the file for the duration of the read. It reads into memory and unlocks immediately. This keeps contention low.
*   **Atomic guarantees:** You will never read a "half-written" line. You will never see two lines mixed together.

---
