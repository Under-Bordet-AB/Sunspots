# Backfill Open-Meteo Module Manual

This manual documents `sunspots_backfill_openmeteo`, a daemon-managed module that fills historical weather data into the SDK DB.

## Purpose

At daemon start, the module checks for missing weather history and fetches missing ranges from Open-Meteo archive API.

It writes through SDK APIs only:

1. `ss_sdk_record_make_f64`
2. `ss_sdk_db_write_record`
3. `ss_sdk_db_get_canonical`

The module does not query SQLite directly.

## Process Model

This is a separate process spawned by daemon module config.

Recommended daemon config:

1. `Timer-type: 1`
2. `Rel-time: 86400`
3. `start_immediately: true`

Behavior:

1. It runs once immediately on daemon startup.
2. It can run again by timer schedule.
3. Daemon does not fail startup if backfill fails.

## Backfill Modes

`backfill.mode`:

1. `oneshot`: run one pass and exit.
2. `maintenance`: run one pass, sleep `daily_hole_check_interval_sec`, repeat forever.

## Supported Metrics

Current module writes:

1. `SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C`
2. `SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT`
3. `SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2`

## Configuration Keys

The daemon passes module JSON as `SUNSPOTS_CONFIG`. Module reads `backfill` and `sdk` blocks.

`backfill` keys:

1. `enabled` (bool): default `true`
2. `mode` (string): `oneshot` or `maintenance`, default `oneshot`
3. `required_history_days` (int): default `14`
4. `chunk_days` (int): default `7`
5. `retry_max_attempts` (int): default `5`
6. `retry_base_backoff_ms` (int): default `1000`
7. `freshness_lag_minutes` (int): default `120` (avoid partial trailing window before latest stable hourly data)
8. `request_interval_ms` (int): default `250`
9. `max_requests_per_minute` (int): default `240`, capped to `600`
10. `max_requests_per_hour` (int): default `2000`, capped to `5000`
11. `max_requests_per_day` (int): default `8000`, capped to `10000`
12. `progress_log_interval_sec` (int): default `10`
13. `daily_hole_check_interval_sec` (int): default `86400`
14. `endpoint` (string): default `https://archive-api.open-meteo.com/v1/archive`
15. `latitude` (double): default `52.52`
16. `longitude` (double): default `13.41`

`sdk` keys follow SDK conventions (for example `db_path`, `log_level`, mirror settings).

## Open-Meteo Rate Limit Notes

Module enforces local limiter windows and clamps configured limits to Open-Meteo published limits:

1. 600 requests/minute
2. 5000 requests/hour
3. 10000 requests/day

If a window is exhausted, module sleeps until the window resets and logs `backfill.rate_limit_wait`.

## Logging Events

Module emits SDK logs with event names:

1. `backfill.config`
2. `backfill.analyze`
3. `backfill.progress`
4. `backfill.fetch_retry`
5. `backfill.rate_limit_wait`
6. `backfill.complete`
7. `backfill.partial`
8. `backfill.detect_failed`
9. `backfill.fill_failed`
10. `backfill.verify_failed`

## Example Module Config

```json
{
  "name": "BackfillOpenMeteo",
  "bin_path": "./build/debug/src/backfill/sunspots_backfill_openmeteo",
  "Timer-type": 1,
  "Rel-time": 86400,
  "start_immediately": true,
  "backfill": {
    "enabled": true,
    "mode": "oneshot",
    "required_history_days": 14,
    "chunk_days": 7,
    "retry_max_attempts": 5,
    "retry_base_backoff_ms": 1000,
    "freshness_lag_minutes": 120,
    "request_interval_ms": 250,
    "max_requests_per_minute": 240,
    "max_requests_per_hour": 2000,
    "max_requests_per_day": 8000,
    "progress_log_interval_sec": 10,
    "daily_hole_check_interval_sec": 86400,
    "endpoint": "https://archive-api.open-meteo.com/v1/archive",
    "latitude": 52.52,
    "longitude": 13.41
  },
  "sdk": {
    "db_path": "db/ss_sdk.db",
    "log_level": "info",
    "log_mirror_enabled": true,
    "log_mirror_path": "logs/sdk.log"
  }
}
```

## Verification

Success criteria for one pass:

1. Process exits `0`.
2. Log contains `backfill.complete`.
3. Required metric windows are readable through `ss_sdk_db_get_canonical`.
