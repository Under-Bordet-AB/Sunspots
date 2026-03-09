# Grejjer att kolla

## Core

### 1. `start_immediately` is ignored by the daemon
- File: `src/core/module.c`
- Relevant code: `module_load()` only reads `start_at_boot`, and `module_timer_config()` only checks `module_start_at_boot`.
- Impact:
  - Any module configured with `"start_immediately": true` will not start immediately.
  - The config can look correct while the daemon silently ignores it.
- Evidence:
  - We had to switch backfill modules in `config/sunspots.json` from `start_immediately` to `start_at_boot` for startup behavior to work.

### 2. Timer-module config hot-reload can preserve stale first-run semantics
- File: `src/core/module.c`
- Relevant code: `module_load()` copies `module_is_first_run` from the old table when config is unchanged.
- Impact:
  - Timer modules can preserve stale startup state across reloads in ways that are hard to reason about.
  - Startup behavior is coupled to reload history, not just current config.

## Compute

### 3. Compute horizon is capped by the shortest input series, then padded with `null`
- File: `src/compute/compute_manager.c`
- Relevant code:
  - `count_horizon_len_from_inputs()`
  - `load_data()`
  - `add_series_to_json()`
  - `add_int64_series_to_json()`
- Impact:
  - If any one input series ends early, `forecast.json` and `result.json` only contain a short valid prefix and then a long tail of `null`.
  - This is exactly what we observed when price coverage was shorter than weather coverage.
- Why it is a bug:
  - The serialization shape suggests a full horizon exists, but the real usable horizon has already collapsed.
  - The caller has no explicit metadata telling it why most of the payload is `null`.

### 4. Compute reads "whatever exists" from the SDK instead of asking for a bounded target horizon
- File: `src/compute/compute_manager.c`
- Relevant code: `ss_sdk_db_get_canonical(0, 0, metrics[m], &samples)` in `load_data()`
- Impact:
  - Compute does not request "the next N slots"; it requests "everything forward from now".
  - Horizon behavior is then an emergent side effect of whichever metric runs out first.
  - This makes call-site behavior fragile and hard to reason about.

### 5. Fresh-data gate ignores irradiance and price
- File: `src/compute/compute_manager.c`
- Relevant code: `wait_for_new_data()`
- Impact:
  - Compute starts as soon as cloud cover and temperature have observed data.
  - It does not wait for irradiance or price, even though both are required later by `count_horizon_len_from_inputs()`.
  - This can produce early compute runs that immediately collapse to a short horizon or fail.

## Fetch

### 6. Old price fetcher writes final published prices as `forecast`
- File: `src/fetch/apis/fetch_elprisjustnu.c`
- Relevant code: `save_to_database()`
- Impact:
  - The module writes `SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH` using `SS_SDK_DATA_FORECAST`.
  - That conflicts with the actual domain contract: published exchange prices are final authoritative slot values, not forecast releases.
  - This is why the live DB ended up with both observed and forecast price rows when the new price backfill was introduced.

### 7. Old price fetcher fails the whole run if either today or tomorrow fails
- File: `src/fetch/apis/fetch_elprisjustnu.c`
- Relevant code: `main()` and `fetch_and_store_for_day()`
- Impact:
  - The fetcher attempts day offsets `0` and `1`.
  - If tomorrow fails, the process exits failure even if today's data was already written.
  - Downstream modules then see partial coverage with no clear contract about whether that is acceptable.

### 8. Generic fetch helper does not check HTTP status codes or set a normal `User-Agent`
- File: `src/fetch/apis/../fetch_utils.h`
- Relevant code: `fetch_from_url()`
- Impact:
  - HTTP `403`, `404`, and similar responses can still look like "successful fetches" at the transport layer.
  - Provider-side filtering based on client fingerprint is invisible until a parser fails later.
  - This is one reason the old `elprisetjustnu` path is brittle.

## Transform

### 9. Old price transform hard-requires exactly 96 points
- File: `src/transform/price/elprisetjustnu.c`
- Relevant code: `transform_elprisetjustnu_price()`
- Impact:
  - Any valid-but-non-96 response is rejected outright.
  - This is brittle for publication timing, partial day availability, DST irregularities, or upstream changes.
  - It turns "usable partial data" into total failure.

### 10. Old price transform ignores `EUR_per_kWh`
- File: `src/transform/price/elprisetjustnu.c`
- Impact:
  - The upstream payload contains both `SEK_per_kWh` and `EUR_per_kWh`.
  - The old transform only keeps SEK.
  - That silently drops already-available canonical data.

## Frontend

### 11. HTTP server bind setup uses `AF_UNSPEC` in an IPv4 socket address
- File: `src/frontend/http_main.c`
- Relevant code: `http_init()`
- Impact:
  - `socket(AF_INET, SOCK_STREAM, 0)` is paired with `sockaddr_in.sin_family = AF_UNSPEC`.
  - That is incorrect address-family pairing and can produce undefined or platform-specific bind behavior.

### 12. Frontend server leaks memory on startup failure paths
- File: `src/frontend/http_main.c`
- Relevant code: `http_init()`
- Impact:
  - If worker allocation or queue allocation fails after earlier allocations succeed, the function returns `NULL` without freeing prior allocations.
  - Static analysis flags this, and the code path is real.

### 13. Frontend standalone/daemon bootstrap is brittle
- File: `src/frontend/frontend_main.c`
- Relevant code: `main()`
- Impact:
  - Runtime behavior depends on a mix of env presence and `ALLOW_STANDALONE_EXEC`.
  - It uses `atoi()` on `SUNSPOTS_SIGNAL`, unchecked JSON field reads, and heap-backed globals with weak cleanup.
  - The startup path is much easier to misconfigure than it should be.

## SDK

### 14. `location.id` is used as a raw DB filename component
- File: `src/sdk/internal/db/ss_db_internal.c`
- Impact:
  - `system.location.id` controls the SQLite filename directly.
  - If not sanitized, path separators or traversal-like values can escape the intended DB naming contract.
  - This is a correctness and safety issue, not just style.

### 15. Read-status semantics are still too implicit for callers
- Files:
  - `src/sdk/ss_sdk.h`
  - `src/sdk/ss_sdk.c`
- Impact:
  - The SDK now distinguishes `OK`, `CLAMPED`, `PARTIAL_DATA`, and `CLAMPED_PARTIAL_DATA`, which is better than before.
  - But callers still only get a status enum and sample list, not structured read metadata.
  - That makes it easy for call sites to mis-handle truncated vs incomplete results unless they are very carefully audited.

### 16. SDK still permits two contradictory meanings for the same canonical via `data_kind`
- Files:
  - `src/sdk/ss_sdk.h`
  - `src/sdk/ss_sdk.c`
- Impact:
  - The SDK permits both `observation` and `forecast` rows for the same canonical series.
  - That is necessary for some weather metrics, but it also allows bad upstream semantics to pollute the DB, which is exactly what happened with price.
  - The SDK does not currently provide any per-canonical guardrail saying "this canonical must never be forecast."

## Backfill

### 17. Weather backfill still mixes two workloads into one long-running worker
- File: `src/backfill/backfill_openmeteo.c`
- Relevant code:
  - archive fill
  - forecast-history fill
- Impact:
  - A user-visible request like "backfill one week" expands into:
    - one week of archive observations
    - plus many hourly forecast-history runs across the same window
  - That makes the worker appear unexpectedly slow and operationally opaque.
  - It is not a crash bug, but it is a real behavior/design bug for operability.

### 18. Weather backfill logs too little progress at normal `INFO` level for long runs
- File: `src/backfill/backfill_openmeteo.c`
- Impact:
  - We moved most detailed progress logs to `DEBUG` to avoid log spam.
  - The result is that a long-running weather backfill can look stalled at `INFO` level even while it is making progress.
  - This is an operational bug because users cannot tell whether the worker is alive, retrying, or actually hung.

### 19. Irradiance observation coverage is hourly, not quarter-hour complete
- Files:
  - `src/backfill/backfill_openmeteo.c`
  - `src/backfill/backfill_payload.c`
- Evidence:
  - In the live DB, `irr_obs` is stored as hourly points with 3600-second gaps between rows.
- Impact:
  - This may be intended because the upstream archive data is hourly, but it means the DB is not "fully filled" at 15-minute resolution for irradiance observations.
  - If callers assume raw observation completeness at slot resolution, they will be misled.

## Summary

The most important cross-module bugs are:

1. `src/fetch/apis/fetch_elprisjustnu.c` stores published prices as `forecast`.
2. `src/transform/price/elprisetjustnu.c` hard-requires exactly 96 points.
3. `src/compute/compute_manager.c` collapses output horizon to the shortest input and then pads with `null`.
4. `src/core/module.c` ignores `start_immediately`.
5. `src/fetch/apis/../fetch_utils.h` does not treat HTTP status and client fingerprint as first-class concerns.

If these modules are revisited later, these should be the first fixes.
