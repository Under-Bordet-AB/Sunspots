# SMHI Backfiller Monitor

Monitor tool to observe comprehensive SMHI canonical records in the database.

## Purpose

This tool continuously monitors the SMHI canonical records stored in the database, displaying periodic updates on data availability and recent values for all supported metrics.

## Configuration

The tool uses its own dedicated configuration file: `config.json`

**Key settings:**
- `enabled`: Enable/disable the monitor
- `database_path`: Path to the SDK canonical database
- `monitor_interval_seconds`: How often to check the database  
- `log_to_file`: Whether to write logs to a file
- `log_file`: Log file path
- `locations`: SMHI locations to monitor (metadata, used for context)

## Supported Metrics

The backfiller can monitor and store the following canonical metrics from SMHI:

**Temperature Metrics:**
- `weather.temperature.air.2m.c` - Air temperature at 2m height
- `weather.temperature.dew.point.c` - Dew point temperature
- `weather.temperature.apparent.c` - Apparent/feels-like temperature

**Humidity & Pressure:**
- `weather.humidity.relative.2m.pct` - Relative humidity
- `weather.pressure.msl.hpa` - Mean sea level pressure

**Wind Metrics:**
- `weather.wind.speed.10m.ms` - Wind speed at 10m height
- `weather.wind.gust.10m.ms` - Wind gust speed at 10m height
- `weather.wind.direction.10m.deg` - Wind direction at 10m height

**Precipitation & Probability:**
- `weather.precip.amount.mm` - Precipitation amount
- `weather.precip.probability.pct` - Precipitation probability
- `weather.precip.thunderstorm.pct` - Thunderstorm probability

**Cloud & Visibility:**
- `weather.cloud.cover.total.pct` - Total cloud cover
- `weather.visibility.km` - Visibility distance
- `weather.fog.probability.pct` - Fog probability

**Radiation & Weather:**
- `weather.radiation.shortwave.wm2` - Shortwave radiation
- `weather.condition.symbol.code` - Weather condition code

## Usage

```bash
# Use default configuration
./run.sh

# Use custom configuration
./run.sh custom_config.json
```

Press `Ctrl+C` to stop the monitor.

## Output

The tool displays periodic status updates like:

```
=== SMHI Backfiller Monitor ===
Database: db/sdk_canonical.db
Monitor interval: 60 seconds
Log file: logs/smhi_backfiller.log
Enabled: yes

[Monitor] Checking database at slot 1771189200
  weather.temperature.air.2m.c: -7.10
  weather.humidity.relative.2m.pct: 85.00
  weather.wind.speed.10m.ms: 6.80
  ... more metrics ...
```

## Building

The tool is built as part of the main build:

```bash
make build
```

Binary location: `build/debug/tools/smhi_backfiller/smhi_backfiller`

## Architecture Note

This is a simplified monitoring tool that reads from the canonical database. In the future, it could be extended to:
- Fetch real-time SMHI API data and write to the database
- Fill gaps in historical data
- Validate data consistency
- Generate reports
- Support additional data sources

