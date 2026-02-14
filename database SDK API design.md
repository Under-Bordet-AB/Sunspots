# Database SDK API Design (Minimal V1)

## 1. Goal

Define the smallest useful SDK DB API for calculator and fetcher modules.

## 2. Fixed Decisions

1. SQLite only.
2. WAL mode enabled.
3. Lazy init on first SDK DB call (no module init call).
4. Only runtime config is `sdk_db.db_path` from daemon env config.
5. Calculator data model is UTC 15-minute slots.
6. Public read API is single-canonical (`ss_sdk_db_get_canonical`).
7. Default read behavior is now-slot only (one 15-minute slot).
8. Canonical DB stores canonical slot values only; source/provenance fields are not part of canonical schema.
9. Read windows use half-open interval semantics: `[start_utc, end_utc)` (`end_utc` exclusive).
10. V1 canonical payload types are limited to `i64` / `f64` / `bool` (no string canonical values in V1).
11. `from_utc` normalization order is strict: validate `from_utc >= 0`; then `0 => current slot`; otherwise floor-align to 15-minute slot start (`aligned = ts - (ts % 900)`).
12. `quarters_to_fetch` normalization is: `0` or `1` => fetch `1` quarter.
13. `quarters_to_fetch` hard cap is `672` quarters (7 days); larger values return `SS_SDK_ERR_INVALID_ARG`.
14. Any function that is not part of an exposed header must be `static` (file-local).
15. `src/sdk/ss_canonical.def` (X-macro catalog) is the single source of truth for canonical IDs, value types, and units.
16. The canonical `records` table only accepts and returns canonicals defined in `ss_canonical.def`.
17. Non-canonical or experimental data must use separate tables and separate APIs (not these V1 canonical APIs).
18. Across calls, canonical reads support `i64`/`f64`/`bool`; each call reads one canonical and returns that canonical's declared value type.
19. Canonical storage enforces one row per `(canonical, data_kind, ts_start_utc)`; reads return at most one selected value per requested slot.
20. Canonical ingest authority is single-writer per canonical slot in V1. Competing writers for the same `(canonical, data_kind, ts_start_utc)` are out of scope and must be routed through non-canonical staging APIs.

## 3. Public SDK API (Minimal)

```c
#include "sdk/ss_canonical.h"  /* ss_metric_id from ss_canonical.def (X-macro list) */
typedef uint8_t ss_sdk_sample_flags;

enum {
    SS_SDK_SAMPLE_OBSERVED     = 1u << 0,
    SS_SDK_SAMPLE_FORECAST     = 1u << 1,
    SS_SDK_SAMPLE_INTERPOLATED = 1u << 2
};

typedef struct {
    int64_t ts_utc;                 /* 15m slot start, UTC epoch seconds */
    ss_metric_id canonical;
    ss_sdk_value_type value_type;
    ss_sdk_value value;
    ss_sdk_sample_flags flags;
} ss_sdk_sample;

typedef struct {
    ss_sdk_sample *samples;
    size_t count;
} ss_sdk_samples_out;

ss_sdk_status ss_sdk_db_write_record(const ss_sdk_record *record);

ss_sdk_status ss_sdk_db_get_canonical(
    int64_t from_utc,               /* validate >=0; 0 => current slot; non-zero auto-floor-aligns (example: 1735689733 => 1735689600) */
    uint16_t quarters_to_fetch,     /* 0 or 1 => fetch 1 quarter; example: 2 => fetch 2 quarters */
    ss_metric_id canonical,
    ss_sdk_samples_out *out
);

void ss_sdk_db_free_samples(ss_sdk_samples_out *out);
void ss_sdk_shutdown(void);
```

## 4. Default Behavior (No Extra Knobs)

1. `ss_sdk_db_get_canonical` validates `from_utc >= 0` before any normalization; negative values return `SS_SDK_ERR_INVALID_ARG`.
2. `from_utc == 0` resolves to current UTC 15m slot (`:00`, `:15`, `:30`, `:45`).
3. For `from_utc > 0`, SDK floor-aligns to 15m slot start: `start_utc = from_utc - (from_utc % 900)`.
4. If floor-alignment from rule 3 yields `start_utc == 0` (for example input `1..899`), start slot is UNIX epoch start (`0`), not current slot.
5. For `ss_sdk_db_get_canonical`, `quarters_to_fetch=0` or `quarters_to_fetch=1` both mean fetch 1 quarter.
6. For `ss_sdk_db_get_canonical`, `quarters_to_fetch > 672` returns `SS_SDK_ERR_INVALID_ARG`.
7. Window slots are exactly `effective_quarters_to_fetch` slots:
   `ts_utc = start_utc + k*900`, where `k in [0, effective_quarters_to_fetch)`.
8. Equivalent interval form is `[start_utc, start_utc + effective_quarters_to_fetch*900)` (`end_utc` exclusive).
9. Selection is based on relation to `now_slot` (not based on first returned element):
10. For `ts < now_slot`, prefer observation; fallback to forecast; then interpolation (if allowed).
11. For `ts == now_slot`, prefer observation; fallback to forecast; then interpolation (if allowed).
12. For `ts > now_slot`, use forecast first; fallback to interpolation (if allowed).
13. Interpolation policy is hardcoded in read-selection code by canonical ID with this V1 map (normative for V1):
    Linear interpolation allowed:
    `SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C`,
    `SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT`,
    `SS_METRIC_WEATHER_WIND_SPEED_10M_MS`,
    `SS_METRIC_WEATHER_WIND_GUST_10M_MS`,
    `SS_METRIC_WEATHER_WIND_DIRECTION_10M_DEG`,
    `SS_METRIC_WEATHER_PRESSURE_MSL_HPA`,
    `SS_METRIC_WEATHER_VISIBILITY_KM`,
    `SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT`,
    `SS_METRIC_WEATHER_CLOUD_COVER_LOW_PCT`,
    `SS_METRIC_WEATHER_CLOUD_COVER_MID_PCT`,
    `SS_METRIC_WEATHER_CLOUD_COVER_HIGH_PCT`,
    `SS_METRIC_WEATHER_PRECIP_AMOUNT_MM`,
    `SS_METRIC_WEATHER_PRECIP_PROBABILITY_PCT`,
    `SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2`,
    `SS_METRIC_ENERGY_FX_SEK_PER_EUR`.
14. Step-expand hourly (no linear interpolation):
    `SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH`,
    `SS_METRIC_ENERGY_PRICE_SPOT_EUR_KWH`.
15. No interpolation:
    `SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE`.
16. Any canonical add/remove in `ss_canonical.def` must update this interpolation map in the same change; implementation must enforce full canonical coverage (no silent fallback policy).
17. Canonical schema disallows multi-row ties within the same class for the same slot (`UNIQUE(canonical, data_kind, ts_start_utc)`).
18. Output sort order is deterministic: `(ts_utc ASC, canonical ASC)`.
19. `SS_SDK_OK` is returned only when the requested canonical has one selected value for every slot in the requested window.
20. `SS_SDK_ERR_PARTIAL_DATA` is returned when one or more requested slots are missing after selection policy, including the case where no rows are returned.
21. Caller provides a non-NULL `out`; SDK overwrites `out->samples` and `out->count`.
22. If `out->samples` is non-NULL after call (including `SS_SDK_ERR_PARTIAL_DATA`), caller must call `ss_sdk_db_free_samples(&out)`.
23. `canonical` must be a valid `ss_metric_id` from `ss_canonical.def`.
24. In V1, caller must explicitly provide one canonical ID per read call.
25. `ss_sdk_db_write_record` must reject any record where canonical ID is unknown or record value type does not match canonical metadata.
26. Canonical read APIs only read canonical rows from the canonical `records` table.
27. Canonical DB rows in V1 must use only `SS_SDK_VALUE_I64`, `SS_SDK_VALUE_F64`, or `SS_SDK_VALUE_BOOL`; any other value type is invalid.

## 5. Error Contract

`ss_sdk_db_write_record`:
1. `SS_SDK_OK`
2. `SS_SDK_ERR_INVALID_ARG`
3. `SS_SDK_ERR_VALIDATION`
4. `SS_SDK_ERR_INTERNAL`

`ss_sdk_db_get_canonical`:
1. `SS_SDK_OK`
2. `SS_SDK_ERR_PARTIAL_DATA`
3. `SS_SDK_ERR_INVALID_ARG`
4. `SS_SDK_ERR_INTERNAL`

Read API notes:
1. Invalid canonical enum IDs return `SS_SDK_ERR_INVALID_ARG`.
2. Validation order is: validate `from_utc >= 0` first, then normalize `from_utc` per rules in Section 4.
3. `quarters_to_fetch > 672` returns `SS_SDK_ERR_INVALID_ARG`.
4. SQLite busy-timeout exhaustion and lock failures map to `SS_SDK_ERR_INTERNAL` in V1.
5. Calculator/default caller policy is strict completeness: treat `SS_SDK_ERR_PARTIAL_DATA` as an error path.
6. Rows that fail canonical/type/shape checks during read are skipped; missing slots caused by skipped rows contribute to `SS_SDK_ERR_PARTIAL_DATA`.
7. `ss_sdk_db_free_samples(&out)` is the required release path for any returned allocation and is safe when `out->samples == NULL`.

## 6. Internal Primitive API (Minimal)

Keep the engine layer small and not module-facing:

```c
typedef struct ss_db_engine ss_db_engine;

ss_sdk_status ss_db_engine_open(const char *db_path, ss_db_engine **out_engine);
void ss_db_engine_close(ss_db_engine **engine);

ss_sdk_status ss_db_engine_write_one(ss_db_engine *engine, const ss_sdk_record *row);

ss_sdk_status ss_db_engine_read_window_15m(
    ss_db_engine *engine,
    ss_metric_id canonical,
    int64_t start_utc,
    int64_t end_utc,                /* exclusive */
    ss_sdk_samples_out *out
);
```

Visibility rules:
1. Only functions declared in `src/sdk/ss_sdk.h` are public SDK symbols.
2. Internal engine entrypoints may be non-`static` only when shared across internal translation units.
3. All other internal helpers must be `static` in their `.c` files.
4. Do not leak helper symbols with external linkage.

## 7. Schema Essentials

1. One canonical `records` table, created as a SQLite `STRICT` table.
2. `UNIQUE` dedupe identity is exactly:
   `(canonical, data_kind, ts_start_utc)`.
3. Required indexes are `(canonical, data_kind, ts_start_utc)`.
4. Required indexes are `(canonical, ts_start_utc)`.
5. Keep forecast and observation rows separate via `data_kind`.
6. `records.canonical` must map to known canonical IDs from `ss_canonical.def`.
7. `records.value_type` must match the canonical's declared value type (from X-macro metadata).
8. Typed payload storage uses `value_i64`, `value_f64`, `value_bool` with a CHECK constraint that exactly one payload column is populated according to `value_type`.
9. Add type checks in constraints (`typeof(...)`) for payload columns to enforce integer/real/boolean encoding by `value_type`, and enforce `value_bool IN (0,1)`.
10. Slot constraints enforce the 15-minute model: `ts_start_utc % 900 = 0` and `ts_end_utc = ts_start_utc + 900`.
11. Canonical schema does not include source/provenance columns (`source_api`, `source_field`, `source_tz`, `model_id`, `model_run_utc`, `issued_at_utc`).
12. If additional non-canonical datasets are needed, place them in separate tables with explicit schema/API boundaries.
13. Write conflict behavior uses `ON CONFLICT ... DO NOTHING` on the unique identity from rule 2:
    keep the first inserted row for that identity; later duplicates for the same identity are ignored.
14. Duplicate drops due to rule 13 must be logged with a dedicated dedupe event so ignored corrections are observable.

## 8. Non-Goals for V1

1. No multi-backend DB abstraction.
2. No public batch-write API.
3. No public provenance/source query API in canonical path.
4. No per-call `as_of` or per-call policy knobs.

## 9. Future V2 Extension Path

If needed later, add one advanced query API with an options struct. Keep V1 minimal API stable.

## 10. Compatibility Policy

1. V1 uses lockstep builds across modules; no cross-version struct negotiation in public API.
2. Public structs intentionally stay simple.
3. Any breaking API/struct change is introduced as a new symbol (for example `ss_sdk_db_get_canonical_v2`).
4. Existing V1 symbols keep behavior stable for the lifetime of V1.
5. Free helpers release owned buffers and reset pointers/count to zero.
6. Symbol visibility is strict: public API symbols are intentional; non-public helpers are `static`.

## 11. Usage Example

```c
/* Single canonical per call. */
ss_sdk_samples_out out = {0};

ss_sdk_status st = ss_sdk_db_get_canonical(
    0,                  /* from_utc: 0 => current aligned slot */
    1,                  /* quarters_to_fetch: 0/1 => 1 quarter */
    SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
    &out
);

if (st == SS_SDK_OK) {
    for (size_t i = 0; i < out.count; ++i) {
        const ss_sdk_sample *s = &out.samples[i];
        /* use s->ts_utc/s->canonical/s->flags and switch on s->value_type */
    }
} else {
    /* strict calculator path: treat PARTIAL_DATA and all errors as failure */
}

ss_sdk_db_free_samples(&out);

/* Another canonical in a separate call. */
ss_sdk_samples_out out_multi = {0};

st = ss_sdk_db_get_canonical(
    explicit_from_utc,   /* auto-aligned by SDK, example: 1735689733 => 1735689600 */
    2,                   /* non-zero example: fetch 2 quarters */
    SS_METRIC_WEATHER_WIND_SPEED_10M_MS,
    &out_multi
);

if (st == SS_SDK_OK) {
    for (size_t i = 0; i < out_multi.count; ++i) {
        const ss_sdk_sample *s = &out_multi.samples[i];
        /* deterministic order: ts_utc ASC, canonical ASC */
    }
} else {
    /* strict calculator path: treat PARTIAL_DATA and all errors as failure */
}

ss_sdk_db_free_samples(&out_multi);
```

## 12. SQLite Runtime Policy

1. Required PRAGMAs on open are `journal_mode=WAL`, `synchronous=NORMAL`, `busy_timeout=5000`, and `wal_autocheckpoint=1000`.
2. SDK relies on SQLite locking (no extra file locks in SDK code).
3. Writes are short transactions (single-record scope in V1 public API).
4. On `SQLITE_BUSY`/`SQLITE_LOCKED` after timeout, return `SS_SDK_ERR_INTERNAL` and log with a dedicated DB-lock error tag.
5. On graceful shutdown, run a passive checkpoint attempt.
