#!/usr/bin/env bash
set -euo pipefail

out_dir="${1:?usage: split_warnings.sh <out_dir> [raw_log_name] }"
raw_name="${2:-build.raw.log}"
workspace_root="$(pwd)"
verbose="${WARNING_SPLITTER_VERBOSE:-0}"
warn_prefix="${WARN_PREFIX:-[warn]}"

mkdir -p "$out_dir"
rm -f "$out_dir"/*.warn.log
raw_log="$out_dir/$raw_name"
: > "$raw_log"

declare -A warning_counts=()
total_warnings=0
suppress_diag_block=0
ignore_current_warning=0

is_ignored_warning_source() {
    local source_path="$1"
    case "$source_path" in
        */cJSON.h|*/cJSON.c)
            return 0
            ;;
    esac
    return 1
}

while IFS= read -r line; do
    printf '%s\n' "$line" >> "$raw_log"

    if [[ "$line" =~ ^(/[^:]+):[0-9]+(:[0-9]+)?:[[:space:]]warning: ]]; then
        source_path="${BASH_REMATCH[1]}"
        if is_ignored_warning_source "$source_path"; then
            suppress_diag_block=1
            ignore_current_warning=1
            continue
        fi
        relative_path="${source_path#$workspace_root/}"
        if [[ "$relative_path" == "$source_path" ]]; then
            relative_path="$(basename "$source_path")"
        fi

        safe_name="$(printf '%s' "$relative_path" | sed -E 's#[^A-Za-z0-9._-]+#_#g')"
        warning_file="$out_dir/${safe_name}.warn.log"
        printf '%s\n' "$line" >> "$warning_file"

        warning_counts["$warning_file"]=$(( ${warning_counts["$warning_file"]:-0} + 1 ))
        total_warnings=$((total_warnings + 1))
        suppress_diag_block=1
        ignore_current_warning=0
        continue
    fi

    if (( suppress_diag_block )); then
        if [[ "$line" =~ ^(/[^:]+):[0-9]+(:[0-9]+)?:[[:space:]]warning: ]]; then
            source_path="${BASH_REMATCH[1]}"
            if is_ignored_warning_source "$source_path"; then
                ignore_current_warning=1
                continue
            fi
            relative_path="${source_path#$workspace_root/}"
            if [[ "$relative_path" == "$source_path" ]]; then
                relative_path="$(basename "$source_path")"
            fi
            safe_name="$(printf '%s' "$relative_path" | sed -E 's#[^A-Za-z0-9._-]+#_#g')"
            warning_file="$out_dir/${safe_name}.warn.log"
            printf '%s\n' "$line" >> "$warning_file"
            warning_counts["$warning_file"]=$(( ${warning_counts["$warning_file"]:-0} + 1 ))
            total_warnings=$((total_warnings + 1))
            ignore_current_warning=0
            continue
        fi

        if [[ "$line" =~ ^(/[^:]+):[0-9]+(:[0-9]+)?:[[:space:]]error: ]]; then
            printf '%s\n' "$line"
            continue
        fi

        if [[ "$line" =~ ^\[[0-9]+/[0-9]+\] ]] || \
           [[ "$line" =~ ^FAILED: ]] || \
           [[ "$line" =~ ^ninja: ]] || \
           [[ "$line" =~ ^Error[[:space:]]while[[:space:]]processing ]] || \
           [[ "$line" =~ ^Found[[:space:]]compiler[[:space:]]error ]]; then
            suppress_diag_block=0
            ignore_current_warning=0
        else
            continue
        fi
    fi

    printf '%s\n' "$line"
done

if (( total_warnings == 0 )); then
    printf '%b no warnings captured\n' "$warn_prefix"
    exit 0
fi

printf '%b wrote %d warnings to files in %s\n' "$warn_prefix" "$total_warnings" "$out_dir"

if (( verbose != 0 )); then
    for warning_file in "${!warning_counts[@]}"; do
        printf '%b %5d warnings -> %s\n' "$warn_prefix" "${warning_counts[$warning_file]}" "$warning_file"
    done | sort -t']' -k3,3nr
fi
