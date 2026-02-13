# Database SDK API Design

## 1. Purpose

Define a stable, backend-neutral database API for the SDK so module developers do not need code changes when storage implementation changes.

Current reality:
- Write rate is low (about once per 1 to 15 minutes).
- Blocking calls are acceptable.
- File locking is desired.
- Compute module currently prefers `atomic_file_rw` style calls.

Primary goal:
- Freeze API contract now.
- Keep backend replaceable later (file -> SQLite -> Postgres) with no caller changes.

## 2. Design Principles

1. Blocking by default.
2. Explicit lock behavior.
3. Canonical record contract in one place.
4. Backend details hidden from callers.
5. Strong correctness over throughput.
6. Compatibility path for legacy `atomic_file_rw` callers.

## 3. Backend Recommendation

### MVP recommendation
Use file backend first.

Why this is acceptable now:
- Very low write volume.
- Simpler deployment.
- Fast implementation and easy debugging.

### Next backend
SQLite should be the first "real DB" backend.

Why SQLite first:
- Embedded, no separate server.
- ACID semantics and indexes.
- Good fit for SDK/library shape.

### Future backend
Postgres can be added later as another backend implementation if multi-host or centralized storage is needed.

## 4. API Stability Contract

Public API must not expose:
- TSV/JSON/file layout
- SQL strings
- Backend-specific handles

Public API must define:
- Open/close lifecycle
- Write semantics
- Read/query semantics
- Error model
- Durability options
- Ordering guarantees

## 5. Proposed Public API (Low-Level First)

```c
// ss_db_api.h (public for now)

#ifndef SS_DB_API_H
#define SS_DB_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "sdk/ss_sdk.h" // ss_sdk_record, status, enums

typedef struct ss_db ss_db; // opaque handle

typedef enum {
    SS_DB_BACKEND_FILE = 0,
    SS_DB_BACKEND_SQLITE = 1,
    SS_DB_BACKEND_POSTGRES = 2
} ss_db_backend;

typedef enum {
    SS_DB_DURABILITY_BEST_EFFORT = 0, // no fsync guarantee
    SS_DB_DURABILITY_FLUSH = 1        // fsync/full durability intent
} ss_db_durability;

typedef struct {
    ss_db_backend backend;
    const char *path_or_dsn;      // file path or DB DSN
    ss_db_durability durability;
    bool blocking_locks;          // true for MVP
} ss_db_options;

typedef struct {
    int64_t ts_start_from_utc;    // inclusive
    int64_t ts_start_to_utc;      // inclusive, 0 means open-ended
    const ss_metric_id *metrics;  // optional filter
    size_t metric_count;
    const char *source_api;       // optional filter
    size_t limit;                 // 0 means no limit
    size_t offset;
} ss_db_query;

typedef int (*ss_db_record_cb)(const ss_sdk_record *rec, void *ctx);

ss_sdk_status ss_db_open(const ss_db_options *opts, ss_db **out_db);
void ss_db_close(ss_db **db);

ss_sdk_status ss_db_write_record(ss_db *db, const ss_sdk_record *record);
ss_sdk_status ss_db_write_records(ss_db *db, const ss_sdk_record *records, size_t count);

ss_sdk_status ss_db_read_records(ss_db *db, const ss_db_query *query, ss_db_record_cb cb, void *ctx);

ss_sdk_status ss_db_delete_before(ss_db *db, int64_t ts_utc);

// Optional transaction controls for future DB backends.
ss_sdk_status ss_db_begin(ss_db *db);
ss_sdk_status ss_db_commit(ss_db *db);
ss_sdk_status ss_db_rollback(ss_db *db);

#endif
```

Notes:
- This is intentionally low-level and explicit.
- Ergonomic wrappers can be added later without changing this core API.

## 6. Canonical Record Rules

For write path (`ss_db_write_record`):

1. Reject non-finite floating values (`NaN`, `+Inf`, `-Inf`).
2. Validate `metric`, `value_type`, and `data_kind` enum ranges.
3. Validate canonical type matches metric metadata.
4. Validate timestamps and forecast requirements.
5. Define record identity key clearly.

### Identity recommendation
Do not use weak dedupe identity.

Use either:
- Full identity tuple (all fields that define unique observation/forecast record), or
- Canonical hash of full identity tuple.

Suggested identity fields:
- `metric`
- `value_type`
- value payload
- `ts_start_utc`
- `ts_end_utc`
- `data_kind`
- `source_api`
- `source_field`
- `source_tz`
- `model_id`
- `model_run_utc`
- `issued_at_utc`

## 7. Blocking and Locking Semantics

For MVP file backend:

1. Every DB call is blocking.
2. Acquire lock at call entry, release before return.
3. Reads use shared lock.
4. Writes use exclusive lock.
5. No busy spin loops.

Implementation note:
- Use `flock` or `fcntl` locks consistently.
- If blocking lock is enabled, wait until lock acquired or signal/error.

## 8. Durability Semantics

Expose durability as API option, not implicit backend behavior.

- `BEST_EFFORT`: write and return, no forced flush.
- `FLUSH`: write + flush (`fsync` for file backend, equivalent for DB backend).

Callers choose durability based on operational needs.

## 9. Query Model

Replace "weeks" as core primitive with explicit time-range query.

Keep weeks wrapper only as convenience:

```c
ss_sdk_status ss_sdk_db_get_last_weeks(int weeks, ss_sdk_record **out_records, size_t *out_count);
```

Wrapper behavior:
- Convert weeks -> `ss_db_query`.
- Call `ss_db_read_records`.

Benefits:
- No API redesign when query requirements grow.
- Backend remains swappable.

## 10. `atomic_file_rw` Compatibility Strategy

Goal: let compute module keep current style while moving storage authority into SDK.

### Step A
Keep existing `af_save` and `af_read` exported.

### Step B
Re-implement internals as thin adapter:
- `af_save(source, type, data)`
  -> map to one or more `ss_sdk_record`
  -> call `ss_db_write_record`
- `af_read(...)`
  -> call `ss_db_read_records`
  -> format output in legacy format expected by compute module.

### Step C
Mark `atomic_file_rw` as compatibility frontend in docs.

Result:
- Compute team does not need immediate rewrite.
- SDK becomes single storage authority.

## 11. Suggested Layered Structure

Layer 1: public low-level DB API (`ss_db_*`)  
Layer 2: SDK domain wrappers (`ss_sdk_db_*`, logs, metric helpers)  
Layer 3: legacy compatibility frontends (`atomic_file_rw` adapter)

This layering supports your "build up from raw API" approach.

## 12. Migration Plan

### Phase 1 (now)
- Implement stable low-level API with file backend.
- Fix correctness issues in current file implementation:
  - weak dedupe
  - non-finite float acceptance
  - zero-write loop handling
  - malformed config silent success

### Phase 2
- Move current `ss_sdk_db_*` to call `ss_db_*` API internally.
- Add `atomic_file_rw` adapter mode.

### Phase 3
- Add SQLite backend behind same `ss_db_*` API.
- No caller changes.

### Phase 4 (optional)
- Add Postgres backend if deployment needs centralized DB.

## 13. What "done" looks like

1. Module callers use stable SDK DB API only.
2. Backend can be switched by configuration.
3. Compute module still works via compatibility adapter.
4. Record correctness and lock semantics are deterministic.
5. Public API docs make no assumptions about file vs SQL backend.

## 14. My opinion for this project

For this workload, file backend is acceptable for MVP if API is designed correctly and correctness bugs are fixed.

If your goal is learning "proper DB" and production-grade internals:
- Keep the stable API above.
- Implement SQLite as the first backend upgrade.
- Keep Postgres as a later backend, not the first step.

That path gives you both:
- Immediate progress with low risk.
- A clean long-term architecture with no downstream breakage.
