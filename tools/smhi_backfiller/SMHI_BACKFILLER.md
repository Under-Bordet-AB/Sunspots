# SMHI Backfiller Tool

A sophisticated weather data backfiller for Sunspots that continuously fetches SMHI (Swedish Meteorological and Hydrological Institute) forecast data and stores it in the SDK database.

## Purpose

The SMHI Backfiller is designed to:
- ✓ **Fill database gaps** with comprehensive weather forecast data
- ✓ **Track historical forecasts** as they update and evolve
- ✓ **Respect rate limits** to avoid API bans
- ✓ **Run continuously** with a live updating console UI
- ✓ **Handle errors gracefully** with retry logic
- ✓ **Shutdown cleanly** on Ctrl+C

## Features

### Live Console UI
The tool displays real-time statistics without scrolling:
- Request counts (total, success, failed)
- Records written to database
- Rate limit hits and error tracking  
- Configured locations
- Configuration parameters  
- Time since last fetch

### Rate Limit Handling
- Respects HTTP 429 (Too Many Requests) responses
- Backs off automatically when rate limited
- Configurable request delays between locations
- No aggressive bulk fetching

### Graceful Shutdown
- Catches SIGINT (Ctrl+C) signal
- Completes current operation
- Closes database cleanly
- Reports statistics on exit

## Configuration

Configuration is loaded from `config/sunspots.json`:

```json
{
  "smhi_backfiller": {
    "enabled": true,
    "locations": [
      {
        "name": "Stockholm",
        "latitude": 59.3293,
        "longitude": 18.0686
      }
    ],
    "forecast_horizon_hours": 72,
    "slot_interval_minutes": 15,
    "fetch_interval_seconds": 300,
    "request_delay_ms": 1000,
    "batch_size": 5,
    "retry_on_error": true,
    "max_retries": 3
  }
}
```

### Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `enabled` | true | Enable/disable the backfiller |
| `locations` | [ ] | Array of locations (lat/lon pairs) |
| `forecast_horizon_hours` | 72 | Hours of forecast to fetch |
| `slot_interval_minutes` | 15 | Data point interval (must match DB schema) |
| `fetch_interval_seconds` | 300 | Seconds between fetch cycles |
| `request_delay_ms` | 1000 | Milliseconds delay between location requests |
| `batch_size` | 5 | Unused (reserved for future) |
| `retry_on_error` | true | Retry failed requests |
| `max_retries` | 3 | Maximum retry attempts |

## Building

```bash
# Build all tools including SMHI backfiller
make build

# Or build just the backfiller
cd build/debug
ninja smhi_backfiller
```

## Running

### Basic Usage
```bash
./build/debug/src/utils/smhi_backfiller [config_file] [db_path]
```

### Examples
```bash
# Use defaults (config/sunspots.json, db/smhi_forecast.db)
./build/debug/src/utils/smhi_backfiller

# Use custom config
./build/debug/src/utils/smhi_backfiller config/sunspots.json

# Use custom config and database
./build/debug/src/utils/smhi_backfiller config/sunspots.json /tmp/test.db

# Run in background
./build/debug/src/utils/smhi_backfiller &

# Run in separate terminal for monitoring
# (Recommended - allows monitoring while other work happens)
```

## SMHI Data Mapping

The tool maps SMHI SNOW1G parameters to Sunspots canonical metrics:

| SMHI Param | SMHI Name | Canonical Metric | Unit |
|-----------|-----------|------------------|------|
| `t` | Temperature | `WEATHER_TEMPERATURE_AIR_2M_C` | °C |
| `r` | Humidity | `WEATHER_HUMIDITY_RELATIVE_2M_PCT` | % |
| `ws` | Wind Speed | `WEATHER_WIND_SPEED_10M_MS` | m/s |
| `wd` | Wind Direction | `WEATHER_WIND_DIRECTION_10M_DEG` | degrees |
| `p` | Pressure | `WEATHER_PRESSURE_MSL_HPA` | hPa |

Additional parameters can be easily added by modifying the mapping logic in `smhi_backfiller.c`.

## Understanding the API

### SMHI SNOW1G Endpoint
```
https://opendata-download-metfcst.smhi.se/api/category/snow1g/version/1/geotype/point/lon/{lon}/lat/{lat}/data.json
```

**Features:**
- 72-hour forecast horizon (default)
- Point-based queries (no grid required)
- 1-hour temporal resolution in API response
- Free public access (CC BY 4.0 SE license)
- No authentication required

### Rate Limit Behavior
- **No hard limits documented**, but SMHI warns against:
  - Rapid sequential requests to many locations
  - Redundant requests for same data
  - Aggressive bulk downloading
- **IP blocking possible** if abuse detected
- **Recommended**: 1-5 second delay between requests

## Monitoring in Action

The console UI shows:
```
╔════════════════════════════════════════════════════════════════════════════════╗
║                   SMHI Weather Data Backfiller                                  ║
╚════════════════════════════════════════════════════════════════════════════════╝

Status:              Running
Uptime:              125 seconds (2 min)
Locations:           3

Requests:            3 total | 3 success | 0 failed
Records Written:     450
Rate Limit Hits:     0
Errors:              0

Config:
  Forecast Horizon:  72 hours
  Slot Interval:     15 minutes
  Fetch Interval:    300 seconds
  Request Delay:     1000 ms

Locations:
  • Stockholm (59.3293°, 18.0686°)
  • Gothenburg (57.7069°, 11.9670°)
  • Malmö (55.6049°, 13.0038°)

Press Ctrl+C to stop gracefully...
```

## Database

### Location
Default: `db/smhi_forecast.db`  
Override: Pass as second argument or set `SS_SDK_DB_PATH`

### Schema
Uses standard Sunspots SDK database schema:
- Table: `records`
- One record per location + metric + time slot
- Data kind: `SS_SDK_DATA_FORECAST` (1)
- Automatic deduplication via `UNIQUE` constraint

### Inspecting Data
```bash
# View schema
sqlite3 db/smhi_forecast.db ".schema"

# Count records
sqlite3 db/smhi_forecast.db "SELECT COUNT(*) FROM records;"

# View recent records
sqlite3 db/smhi_forecast.db "SELECT * FROM records ORDER BY ts_start_utc DESC LIMIT 10;"

# Count by metric
sqlite3 db/smhi_forecast.db "SELECT canonical, COUNT(*) FROM records GROUP BY canonical;"
```

## Error Handling

The tool handles common errors:

| Error | Action | Details |
|-------|--------|---------|
| API unreachable | Retry 3× | Waits 5 seconds between retries |
| HTTP 429 (rate limited) | Back off | Sleeps 60 seconds |
| Invalid JSON | Log & skip | Continues to next location |
| Database write failure | Log & continue | May skip some records |
| SIGINT (Ctrl+C) | Graceful shutdown | Waits for current operation |

## Performance Considerations

### Typical Behavior
- **3 locations**: ~10-15 seconds per fetch cycle
- **Records per location**: ~300-400 per fetch
- **Database writes**: ~1500-2000 records per cycle
- **Disk I/O**: Minimal (WAL mode, efficient writes)

### Optimization
- Increase `request_delay_ms` if rate limited
- Increase `fetch_interval_seconds` if API slow  
- Decrease for faster backfilling of historical data
- Monitor database size with `sqlite3 db.db ".tables"`

## Troubleshooting

### Tool Not Fetching Data
1. Check network connectivity: `curl https://opendata-download-metfcst.smhi.se/`
2. Verify config file exists and is valid JSON
3. Verify locations have valid coordinates
4. Check database directory is writable

### Rate Limited
- Increase `request_delay_ms` in config (default: 1000ms)
- Increase `fetch_interval_seconds` (default: 300s)
- Contact SMHI if legitimate use case: `kundtjanst@smhi.se`

### Database Errors
- Ensure `db/` directory exists and is writable
- Check disk space
- Verify no other process has lock on database

### High Memory Usage
- Normal for initial fill (~100MB for 3+ locations)
- Stabilizes after ~1000+ records written
- Consider reducing number of locations if constrained

## Integration with Sunspots

The backfiller is part of the utils module:
- **Source**: `src/utils/smhi_backfiller.c`
- **Build**: Configured in `src/utils/CMakeLists.txt`
- **Config**: Defined in `config/sunspots.json`
- **Database**: Uses SDK public API (`ss_sdk_db_write_record`)

## Testing

### Quick Test (10 seconds)
```bash
timeout 10 ./build/debug/src/utils/smhi_backfiller
```

### Full Test (5 minute collection)
```bash
timeout 300 ./build/debug/src/utils/smhi_backfiller &
# Monitor in another terminal
sqlite3 db/smhi_forecast.db "SELECT COUNT(*) FROM records;"
```

## Licensing

- **SMHI Data**: Creative Commons Attribution 4.0 (CC BY 4.0 SE)
- **Must attribute**: SMHI and any modifications
- **Commercial use**: Permitted

## Future Enhancements

Potential improvements:
- [ ] Historical data backfilling (older forecasts)
- [ ] Multiple forecast models (MEPS, HIRLAM)
- [ ] Precipitation data support
- [ ] Cloud cover detailed breakdown
- [ ] Web UI for monitoring
- [ ] Database pruning (automatic old data removal)
- [ ] Metrics export (Prometheus format)
- [ ] Email alerts for errors

## Support

For issues:
1. Check `db/` directory permissions
2. Verify network access to SMHI API
3. Review config syntax in config/sunspots.json
4. Check stderr output for detailed errors
5. Contact SMHI support: `kundtjanst@smhi.se`

## See Also

- [SMHI Open Data](https://opendata.smhi.se/)
- [Sunspots SDK Documentation](../src/sdk/)
- [Project README](../../README.md)
