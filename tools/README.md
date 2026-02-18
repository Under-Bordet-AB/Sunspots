# SDK Database Test Tool

This is a standalone testing utility for the Sunspots SDK database module. It demonstrates how the SDK stores and retrieves canonical weather data.

## What It Does

1. **Fetches live weather data** from the Open-Meteo API (temperature, humidity, wind, etc.)
2. **Transforms and writes** the data as SDK canonical records to the SQLite database
3. **Reads records back** from the database
4. **Outputs the results as JSON** for verification

## Building

The tool is built automatically with the main project:

```bash
make build
```

Or build just the test tool:

```bash
cd build/debug
ninja sdk_db_test
```

## Running

Using the provided script (recommended):

```bash
./tools/run_sdk_db_test.sh [latitude] [longitude] [output_file] [db_path]
```

Examples:

```bash
# Test with Stockholm (default)
./tools/run_sdk_db_test.sh

# Test with London
./tools/run_sdk_db_test.sh 51.5074 -0.1278 london.json

# Test with custom database path
./tools/run_sdk_db_test.sh 59.3293 18.0686 stockholm.json /tmp/test.db
```

Or run the binary directly:

```bash
./build/debug/tools/sdk_db_test [latitude] [longitude] [output_file] [db_path]
```

## Output

The tool generates a JSON file with the fetched and verified data:

```json
{
  "records": [
    {
      "ts_utc": 1771188300,
      "metric_id": 0,
      "value_type": "f64",
      "value": -7,
      "flags": "observed"
    },
    ...
  ],
  "fetch_timestamp": 1771188603,
  "start_utc": 1771188300
}
```

Each record shows:
- `ts_utc` - Unix timestamp (aligned to 15-minute slots)
- `metric_id` - The canonical metric identifier
- `value_type` - Data type: `f64`, `i64`, or `bool`
- `value` - The actual value
- `flags` - Whether the sample is `observed` or `interpolated`

## Canonical Metrics

The tool writes and reads these metrics from Open-Meteo:

| ID | Metric | Unit | Type |
|----|----|------|------|
| 0 | Temperature (2m) | °C | f64 |
| 1 | Relative Humidity (2m) | % | f64 |
| 2 | Wind Speed (10m) | m/s | f64 |
| 4 | Wind Direction (10m) | degrees | f64 |
| 14 | Weather Condition Code | - | i64 |

See [ss_canonical.def](../src/sdk/ss_canonical.def) for the complete list of available canonical metrics.

## Understanding the SDK Database

The SDK database stores **canonical 15-minute slot records**:

1. **15-minute slots**: All timestamps are aligned to 900-second boundaries (e.g., 10:00:00, 10:15:00, 10:30:00)
2. **Multi-metric**: Each slot can store multiple different metrics
3. **Observation vs Forecast**: Records are marked as either observations or forecasts
4. **Interpolation support**: Retrieved data can include interpolated samples
5. **Thread-safe**: Uses SQLite with WAL mode and mutexes for concurrent access

## Database File

The SQLite database file contains:

- **records** table: Stores canonical metrics with timestamps, values, and metadata
- **Indexes**: For fast queries by metric, data kind, and timestamp

Default location: `db/ss_sdk.db`

To inspect the database directly:

```bash
sqlite3 db/ss_sdk.db
sqlite> .schema
sqlite> SELECT count(*) FROM records;
sqlite> SELECT * FROM records LIMIT 5;
```

## Environment Variables

- `SS_SDK_DB_PATH` - Override the default database location (set by the test tool)

## Troubleshooting

### Database not created
- Check that the `db/` directory is writable
- Verify no file permission issues

### No records read back
- Ensure records were written (check stderr output)
- Verify the timestamp alignment (must be 900-second aligned boundaries)

### Network errors
- The tool requires internet access to reach `api.open-meteo.com`
- Check your firewall and DNS settings

### JSON parsing errors
- Ensure the Open-Meteo response is valid JSON
- Check the API endpoint format

## Source Code

- **Tool**: [tools/sdk_db_test.c](sdk_db_test.c)
- **SDK Database**: [src/sdk/internal/db/ss_db_internal.c](../src/sdk/internal/db/ss_db_internal.c)
- **SDK Header**: [src/sdk/ss_sdk.h](../src/sdk/ss_sdk.h)

## Testing the Integration

This tool is useful for:
- ✓ Verifying the SDK database works standalone
- ✓ Testing database write/read cycles
- ✓ Inspecting what data is actually stored
- ✓ Debugging timestamp alignment issues
- ✓ Validating metric type conversions
