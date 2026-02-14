# Database SDK API Design (Minimal V1)

## 1. Goal

Define the smallest useful SDK DB API for calculator and fetcher modules.

## 2. Fixed Decisions

1. SQLite only.
2. WAL mode enabled.
3. Lazy init on first SDK DB call (no module init call).
4. Only runtime config is `sdk_db.db_path` from daemon env config.
5. Calculator data model is UTC 15-minute slots.
6. Public read path is multi-canonical first.
7. Default read behavior is now-slot plus forecast horizon.
8. Keep forecast vintages in storage, but default reads return one selected value per slot.
9. Public V1 keeps minimal knobs and minimal parameter count.
10. Any function that is not part of an exposed header must be `static` (file-local).

## 3. Public SDK API (Minimal)

```c
typedef int32_t ss_sdk_canonical_code;
typedef uint8_t ss_sdk_sample_flags;

#define SS_SDK_DEFAULT_HORIZON_SLOTS 96u  /* 24h * 4 slots/hour */

enum {
    SS_SDK_SAMPLE_OBSERVED     = 1u << 0,
    SS_SDK_SAMPLE_FORECAST     = 1u << 1,
    SS_SDK_SAMPLE_INTERPOLATED = 1u << 2
};

typedef struct {
    int64_t ts_utc;                 /* 15m slot start, UTC epoch seconds */
    ss_sdk_canonical_code canonical;
    double value;
    ss_sdk_sample_flags flags;
    uint8_t reserved[3];            /* explicit padding + forward growth */
} ss_sdk_sample_f64;

typedef struct {
    int64_t hole_start_utc;         /* inclusive */
    int64_t hole_end_utc;           /* exclusive */
    uint64_t missing_buckets;
    uint64_t reserved0;             /* forward growth / explicit 8-byte align */
} ss_sdk_time_hole;

typedef struct {
    uint64_t size_bytes;            /* must be sizeof(ss_sdk_samples_out) */
    ss_sdk_sample_f64 *samples;
    size_t count;
    uint64_t reserved[4];           /* consume before adding new fields */
} ss_sdk_samples_out;

typedef struct {
    uint64_t size_bytes;            /* must be sizeof(ss_sdk_holes_out) */
    ss_sdk_time_hole *holes;
    size_t count;
    uint64_t reserved[4];           /* consume before adding new fields */
} ss_sdk_holes_out;

ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record);

ss_sdk_status ss_sdk_db_get_canonical(
    int64_t from_utc,               /* 0 => resolve to current 15m slot */
    ss_sdk_canonical_code canonical,
    ss_sdk_samples_out *out
);

ss_sdk_status ss_sdk_db_get_canonicals(
    int64_t from_utc,               /* 0 => resolve to current 15m slot */
    const ss_sdk_canonical_code *canonicals,
    uint32_t canonical_count,
    ss_sdk_samples_out *out
);

ss_sdk_status ss_sdk_db_get_holes_15m(
    int64_t from_utc,               /* 0 => resolve to current 15m slot */
    const ss_sdk_canonical_code *canonicals,
    uint32_t canonical_count,
    ss_sdk_holes_out *out
);

void ss_sdk_db_free_samples(ss_sdk_samples_out *out);
void ss_sdk_db_free_holes(ss_sdk_holes_out *out);
void ss_sdk_shutdown(void);
```

## 4. Default Behavior (No Extra Knobs)

1. `from_utc=0` means current UTC 15m slot (`:00`, `:15`, `:30`, `:45`).
2. Non-zero `from_utc` must be 15m aligned, else `SS_SDK_ERR_INVALID_ARG`.
3. Window is `[from_slot, from_slot + SS_SDK_DEFAULT_HORIZON_SLOTS*900)`.
4. First slot selection order is observed, else forecast, else interpolation if allowed.
5. Future slots use latest-issued forecast.
6. Continuous canonicals can be interpolated.
7. Hourly electricity prices are step-expanded to 15m (not linear interpolation).
8. Output sort order is deterministic: `(ts_utc ASC, canonical ASC)`.
9. Caller initializes `out->size_bytes`; SDK validates it and fills `out->samples/out->holes` and `out->count`.

## 5. Error Contract

`ss_sdk_db_write_record`:
1. `SS_SDK_OK`
2. `SS_SDK_ERR_INVALID_ARG`
3. `SS_SDK_ERR_VALIDATION`
4. `SS_SDK_ERR_INTERNAL`

`ss_sdk_db_get_canonical`, `ss_sdk_db_get_canonicals`, `ss_sdk_db_get_holes_15m`:
1. `SS_SDK_OK`
2. `SS_SDK_ERR_PARTIAL_DATA`
3. `SS_SDK_ERR_INVALID_ARG`
4. `SS_SDK_ERR_VALIDATION`
5. `SS_SDK_ERR_INTERNAL`

## 6. Internal Primitive API (Minimal)

Keep the engine layer small and not module-facing:

```c
typedef struct ss_db_engine ss_db_engine;

ss_sdk_status ss_db_engine_open(const char *db_path, ss_db_engine **out_engine);
void ss_db_engine_close(ss_db_engine **engine);

ss_sdk_status ss_db_engine_write_one(ss_db_engine *engine, const ss_sdk_record *row);

ss_sdk_status ss_db_engine_read_window_15m(
    ss_db_engine *engine,
    const ss_sdk_canonical_code *canonicals,
    uint32_t canonical_count,
    int64_t start_utc,
    int64_t end_utc,
    ss_sdk_samples_out *out
);

ss_sdk_status ss_db_engine_get_holes_window_15m(
    ss_db_engine *engine,
    const ss_sdk_canonical_code *canonicals,
    uint32_t canonical_count,
    int64_t start_utc,
    int64_t end_utc,
    ss_sdk_holes_out *out
);
```

Visibility rules:
1. Only functions declared in `src/sdk/ss_sdk.h` are public SDK symbols.
2. Internal engine entrypoints may be non-`static` only when shared across internal translation units.
3. All other internal helpers must be `static` in their `.c` files.
4. Do not leak helper symbols with external linkage.

## 7. Schema Essentials

1. One canonical `records` table.
2. `UNIQUE` on full logical identity for dedupe.
3. Required indexes are `(canonical, data_kind, ts_start_utc)`.
4. Required indexes are `(canonical, ts_start_utc, issued_at_utc DESC)`.
5. Keep forecast and observation rows separate via `data_kind`.

## 8. Non-Goals for V1

1. No multi-backend DB abstraction.
2. No public batch-write API.
3. No public "all forecast vintages" API in default calculator path.
4. No per-call source filter, per-call `as_of`, or per-call policy knobs.

## 9. Future V2 Extension Path

If needed later, add one advanced query API with an options struct. Keep V1 minimal API stable.

## 10. ABI Notes

1. Public output structs use `size_bytes` and `reserved[]` for forward-compatible growth.
2. Caller must zero-init output structs and set `size_bytes` before calling read APIs.
3. Free helpers release owned buffers and reset pointers/count to zero.
4. Symbol visibility is strict: public API symbols are intentional; non-public helpers are `static`.

## 11. Usage Example

```c
/* Single canonical: TEMP_C from current 15m slot forward. */
ss_sdk_samples_out out = {0};
out.size_bytes = sizeof(out);

ss_sdk_status st = ss_sdk_db_get_canonical(
    0,                  /* from_utc: 0 => now slot */
    SS_CANONICAL_TEMP_C,
    &out
);

if (st == SS_SDK_OK || st == SS_SDK_ERR_PARTIAL_DATA) {
    for (size_t i = 0; i < out.count; ++i) {
        const ss_sdk_sample_f64 *s = &out.samples[i];
        /* use s->ts_utc, s->value, s->flags */
    }
}

ss_sdk_db_free_samples(&out);

/* Multi-canonical: TEMP_C + WIND_MS from explicit aligned start time. */
ss_sdk_canonical_code wanted[] = { SS_CANONICAL_TEMP_C, SS_CANONICAL_WIND_MS };
ss_sdk_samples_out out_multi = {0};
out_multi.size_bytes = sizeof(out_multi);

st = ss_sdk_db_get_canonicals(
    aligned_from_utc,
    wanted,
    2u,
    &out_multi
);

if (st == SS_SDK_OK || st == SS_SDK_ERR_PARTIAL_DATA) {
    for (size_t i = 0; i < out_multi.count; ++i) {
        const ss_sdk_sample_f64 *s = &out_multi.samples[i];
        /* deterministic order: ts_utc ASC, canonical ASC */
    }
}

ss_sdk_db_free_samples(&out_multi);
```
