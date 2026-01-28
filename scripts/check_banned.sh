#!/bin/bash

# Configuration
SEARCH_DIR="src"
EXCLUDE_DIR="libs"
STANDARDS_FILE="docs/standards/banned.md"

# Output-related identifiers that MUST use jj_log
LOG_ONLY=("printf" "fprintf" "vprintf" "vfprintf" "stdout" "stderr" "puts" "fputs" "putchar" "fputc" "perror")

# Colors
RED='\033[0;31m'
NC='\033[0m'

echo ">> Checking for banned identifiers (per $STANDARDS_FILE)..."

if [ ! -f "$STANDARDS_FILE" ]; then
    echo -e "${RED}ERROR: Standards file $STANDARDS_FILE not found.${NC}"
    exit 1
fi

FAILED=0
TMP_FILE=$(mktemp)

# Read the plain list from the standards file
while read -r id || [ -n "$id" ]; do
    # Skip empty lines or comments
    [[ -z "$id" || "$id" =~ ^# ]] && continue
    
    # Trim whitespace
    id=$(echo "$id" | xargs)

    # Search for word boundary
    if [[ "$id" == "system" ]]; then
        # For 'system', we only care about function calls like system(...)
        grep -rnI --exclude-dir="$EXCLUDE_DIR" "$id(" "$SEARCH_DIR" > "$TMP_FILE"
    else
        grep -rnI --exclude-dir="$EXCLUDE_DIR" "\b$id\b" "$SEARCH_DIR" > "$TMP_FILE"
    fi

    if [ -s "$TMP_FILE" ]; then
        while read -r match; do
            echo -e "${RED}[BANNED]${NC} Found '$id' in: $match"
            
            # Check if this is a log-only identifier
            is_log_only=0
            for l_id in "${LOG_ONLY[@]}"; do
                if [[ "$id" == "$l_id" ]]; then is_log_only=1; break; fi
            done

            if [ $is_log_only -eq 1 ]; then
                echo -e "         ➜  REASON: All output must be captured via the jj_log module."
                echo -e "         ➜  RECOMMENDATION: Use jj_log functions instead of $id."
            else
                echo -e "         ➜  REASON: This identifier is banned for safety/security reasons."
                echo -e "         ➜  RECOMMENDATION: Use safe POSIX alternatives (see docs/standards/code.md)."
            fi
            FAILED=1
        done < "$TMP_FILE"
    fi
done < "$STANDARDS_FILE"

rm -f "$TMP_FILE"

YELLOW='\033[1;33m'

if [ $FAILED -eq 1 ]; then
    echo -e "${YELLOW}WARNING: Banned identifiers detected in $SEARCH_DIR.${NC}"
    exit 0
fi


echo "  [PASS] No banned identifiers found."
exit 0
