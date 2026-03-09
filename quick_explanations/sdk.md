# SDK

## Purpose

The SDK is the canonical runtime data layer.

It owns:

- canonical metric definitions
- SQLite persistence
- read/write validation
- selection between observation and forecast rows
- interpolation policy
- structured logging

For the rest of the system, the SDK is supposed to be the authoritative interface for time-series storage.

## Key files

- `src/sdk/ss_sdk.h`
- `src/sdk/ss_sdk.c`
- `src/sdk/internal/db/ss_db_internal.c`
- `src/sdk/internal/db/ss_db_internal_read.c`
- `src/sdk/internal/log/ss_log_internal.c`
- `src/sdk/internal/ss_sdk_config*.c`

## How it works

Public API:

- `ss_sdk_record_make_*`
- `ss_sdk_db_write_record`
- `ss_sdk_db_write_records`
- `ss_sdk_db_get_canonical`
- `ss_sdk_db_free_samples`
- logging helpers

Runtime model:

- process-local singleton DB/log state
- lazy DB open
- automatic teardown through `atexit`
- config from `SUNSPOTS_SYSTEM`
- DB identity from `system.location.id`

Data model:

- 15-minute slots
- observation vs forecast separation
- one release identity dimension via `ingested_utc`
- read-time selection and interpolation

## Design strengths

- The architecture is good. A single canonical storage API is the right foundation for the system.
- The SDK is stricter than the surrounding modules, which is what you want from a boundary layer.
- Batch writes are atomic.
- Forecast release versioning through `ingested_utc` is a strong design choice.
- The DB layer stores location identity/meta explicitly instead of pretending the filename alone is enough.

## Main weaknesses

- Internal complexity is still concentrated in a few deep DB/read/log functions.
- Public read statuses are more expressive than before, but call-site compatibility still needs discipline.
- A lot of important behavior is implicit rather than carried in explicit result metadata.
- The process-global singleton model is pragmatic, but it makes internal state boundaries less explicit.

## Critique

This is solid systems code, not junior code. The biggest positive is that the SDK is clearly trying to be authoritative rather than a thin convenience wrapper.

The main architectural tension is this:

- the public API is fairly compact
- the true behavior is rich and policy-heavy underneath

That creates pressure in three places:

1. result semantics
2. internal complexity
3. caller expectations

The SDK is strongest when writing data. Validation and persistence are coherent.

The read path is where the design gets harder. It has to combine:

- bounded vs forward reads
- obs vs forecast precedence
- interpolation policy
- completeness signaling
- clamping behavior

That is why so much complexity remains in `ss_db_internal.c` and `ss_db_internal_read.c`.

## What is good now

- `system.location.id` as DB identity is the correct direction.
- removing production `shutdown()` from the public API was the right move
- keeping `ingested_utc = 0` as backward-compatible caller behavior was the right move
- splitting `CLAMPED` from `PARTIAL_DATA` was a real improvement

## What still needs care

- `location.id` should be treated as an identifier, not an unchecked path fragment
- public status changes need explicit caller compatibility handling
- a future read metadata struct would simplify both callers and tests
- internal DB/log helpers still deserve another decomposition pass

## Quality verdict

High-quality core design, medium-to-high internal complexity, still the strongest architectural subsystem in the repo.
