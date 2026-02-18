#!/bin/bash
#
# SMHI Backfiller Runner
#
# Convenient script to run the Sunspots SMHI weather data backfiller
# with proper configuration and database path setup.
#
# Usage: ./run_smhi_backfiller.sh [config_file] [db_path]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BIN="${PROJECT_ROOT}/build/debug/tools/smhi_backfiller/smhi_backfiller"

if [ ! -f "$BIN" ]; then
    echo "Error: smhi_backfiller binary not found at $BIN"
    echo "Please run: make build"
    exit 1
fi

# Default paths - uses tool's local config and shared db/logs directories
CONFIG_FILE="${1:-${SCRIPT_DIR}/config.json}"
DB_PATH="${2:-${PROJECT_ROOT}/db/smhi_forecast.db}"
LOG_FILE="${PROJECT_ROOT}/logs/smhi_backfiller.log"

# Ensure database and log directories exist
DB_DIR="$(dirname "$DB_PATH")"
LOG_DIR="$(dirname "$LOG_FILE")"
mkdir -p "$DB_DIR" "$LOG_DIR"

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║         SMHI Backfiller - Weather Data Collection              ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
echo "Configuration File: $CONFIG_FILE"
echo "Database Path:      $DB_PATH"
echo "Log File:           $LOG_FILE"
echo ""
echo "This tool will continuously fetch SMHI forecast data and store"
echo "it in the database. It respects rate limits and handles errors."
echo ""
echo "Starting backfiller... (Press Ctrl+C to stop gracefully)"
echo ""

# Run the backfiller
cd "$PROJECT_ROOT"
"$BIN" "$CONFIG_FILE" "$DB_PATH"
