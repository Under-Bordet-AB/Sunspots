# SDK Database Test Tool

Simple tool to read and export canonical records from the SDK database.

## Purpose

This tool reads metric records from the SMHI canonical database and exports them as JSON for inspection and testing.

## Configuration

The tool uses its own dedicated configuration file: `config.json`

**Key settings:**
- `database.path`: Path to the SDK database
- `output.file`: Output JSON filename
- `metrics.enabled`: List of metrics to read
- `query.quarters_to_read`: Number of 15-minute quarters to read

## Usage

```bash
# Use default settings (outputs to logs/sdk_output.json)
./run.sh

# Custom output file
./run.sh logs/custom_output.json

# Custom output, database path, and quarters
./run.sh logs/output.json db/custom.db 4
```

## Output Format

The tool outputs a JSON file with records in the format:

```json
{
  "records": [
    {
      "ts_utc": 1771189200,
      "metric_id": "weather.temperature.air.2m.c",
      "value_type": "f64",
      "value": -7.1,
      "flags": "observed"
    }
  ],
  "fetch_timestamp": 1771189816,
  "start_utc": 1771189200
}
```

Note: `metric_id` is output as a stable canonical string (from X macros), not a numeric ID. This ensures compatibility if the enum order ever changes.

## Building

The tool is built as part of the main build:

```bash
make build
```

Binary location: `build/debug/tools/sdk_db_test/sdk_db_test`
