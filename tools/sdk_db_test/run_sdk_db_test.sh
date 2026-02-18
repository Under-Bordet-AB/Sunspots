#!/bin/bash
#
# SDK Database Test Tool
# 
# This script demonstrates the Sunspots SDK database functionality by:
# 1. Fetching live weather data from Open-Meteo API
# 2. Writing canonical records to the SQLite database
# 3. Reading records back and saving as JSON
#
# Usage: ./tools/run_sdk_db_test.sh [latitude] [longitude] [output_file] [db_path]
#
# Examples:
#   ./tools/run_sdk_db_test.sh                                    # Stockholm (default)
#   ./tools/run_sdk_db_test.sh 51.5074 -0.1278 london.json       # London
#   ./tools/run_sdk_db_test.sh 40.7128 -74.0060 newyork.json     # New York

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BIN="${PROJECT_ROOT}/build/debug/tools/sdk_db_test"

if [ ! -f "$BIN" ]; then
    echo "Error: sdk_db_test binary not found at $BIN"
    echo "Please run: make build"
    exit 1
fi

# Default parameters
LATITUDE="${1:-59.3293}"      # Stockholm latitude
LONGITUDE="${2:-18.0686}"     # Stockholm longitude  
OUTPUT="${3:-sdk_output.json}"
DB_PATH="${4:-db/sdk_test.db}"

echo "Running SDK Database Test Tool"
echo "==============================================="
echo "Location: $LATITUDE, $LONGITUDE"
echo "Database: $DB_PATH"
echo "Output: $OUTPUT"
echo ""

cd "$PROJECT_ROOT"
"$BIN" "$LATITUDE" "$LONGITUDE" "$OUTPUT" "$DB_PATH"

echo ""
echo "Database file created: $(ls -lh "$DB_PATH" | awk '{print $5, $9}')"
echo ""
echo "View the JSON output:"
echo "  cat $OUTPUT"
echo ""
echo "Pretty-print the JSON:"
echo "  cat $OUTPUT | jq ."
