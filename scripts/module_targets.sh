#!/bin/sh
set -eu

usage() {
  cat >&2 <<'EOF'
Usage:
  scripts/module_targets.sh list <build_dir>
  scripts/module_targets.sh list-aliases <build_dir>
  scripts/module_targets.sh resolve <build_dir> <selector_expr>

selector_expr accepts comma/space-separated selectors:
  - exact CMake target name (example: sunspots_daemon)
  - alias without sunspots_ prefix (example: daemon)
  - fuzzy module token (example: fetch)
  - all
EOF
  exit 2
}

require_build_dir() {
  build_dir="$1"
  if [ ! -f "$build_dir/CMakeCache.txt" ]; then
    echo "Build directory is not configured: $build_dir" >&2
    echo "Run the corresponding configure/build step first." >&2
    exit 1
  fi
}

list_targets_from_link_files() {
  build_dir="$1"
  find "$build_dir" -type f -path "*/CMakeFiles/*.dir/link.txt" ! -path "*/_deps/*" 2>/dev/null \
    | while IFS= read -r link_file; do
      target_dir=$(dirname "$link_file")
      target_name=$(basename "$target_dir" .dir)
      first_line=$(sed -n '1p' "$link_file")
      out_path=$(printf '%s\n' "$first_line" | awk '{for (i = 1; i <= NF; i++) if ($i == "-o") { print $(i+1); exit }}')
      [ -n "$out_path" ] || continue
      out_base=$(basename "$out_path")
      case "$out_base" in
        lib*.a|lib*.so|lib*.dylib|*.o)
          continue
          ;;
      esac
      printf '%s\n' "$target_name"
    done | sort -u
}

list_targets_from_target_help() {
  build_dir="$1"
  cmake --build "$build_dir" --target help 2>/dev/null \
    | sed -n 's/^[.][.][.] //p' \
    | awk '{ print $1 }' \
    | grep -E '^(sunspots_|.*_test$|.*_benchmark$|.*_fuzzer$)' \
    | grep -Ev '^sunspots_(cjson|compute|config|curly|frontend_core|linked_list|sdk|utils|weather_transform)$' \
    | sort -u
}

list_executable_targets() {
  build_dir="$1"
  from_link_files=$(list_targets_from_link_files "$build_dir" || true)
  if [ -n "$from_link_files" ]; then
    printf '%s\n' "$from_link_files"
    return 0
  fi
  list_targets_from_target_help "$build_dir"
}

print_aliases() {
  while IFS= read -r target; do
    [ -n "$target" ] || continue
    alias="$target"
    case "$target" in
      sunspots_*)
        alias=${target#sunspots_}
        ;;
    esac
    printf '%s\t%s\n' "$target" "$alias"
  done
}

resolve_selector() {
  selector="$1"
  targets_file="$2"
  selector=$(printf '%s' "$selector" | tr -d '[:space:]')
  [ -n "$selector" ] || return 0

  if [ "$selector" = "all" ]; then
    cat "$targets_file"
    return 0
  fi

  exact=$(awk -v s="$selector" '$0 == s { print; found = 1 } END { if (!found) exit 1 }' "$targets_file" || true)
  if [ -n "$exact" ]; then
    printf '%s\n' "$exact"
    return 0
  fi

  prefixed="sunspots_$selector"
  prefixed_exact=$(awk -v s="$prefixed" '$0 == s { print; found = 1 } END { if (!found) exit 1 }' "$targets_file" || true)
  if [ -n "$prefixed_exact" ]; then
    printf '%s\n' "$prefixed_exact"
    return 0
  fi

  alias_match=$(awk -v s="$selector" '
    {
      t = $0
      a = t
      if (t ~ /^sunspots_/) a = substr(t, 10)
      if (a == s) print t
    }
  ' "$targets_file")
  if [ -n "$alias_match" ]; then
    printf '%s\n' "$alias_match"
    return 0
  fi

  fuzzy=$(awk -v s="$selector" '
    {
      t = $0
      a = t
      if (t ~ /^sunspots_/) a = substr(t, 10)
      if (index(t, s) > 0 || index(a, s) > 0) print t
    }
  ' "$targets_file")
  if [ -n "$fuzzy" ]; then
    printf '%s\n' "$fuzzy"
    return 0
  fi

  return 1
}

resolve_targets() {
  build_dir="$1"
  selector_expr="${2-}"
  targets_tmp=$(mktemp)
  selected_tmp=$(mktemp)
  trap 'rm -f "$targets_tmp" "$selected_tmp"' EXIT INT TERM

  list_executable_targets "$build_dir" > "$targets_tmp"
  if [ ! -s "$targets_tmp" ]; then
    echo "No executable targets discovered under $build_dir" >&2
    exit 1
  fi

  if [ -z "$selector_expr" ] || [ "$selector_expr" = "all" ]; then
    cat "$targets_tmp"
    return 0
  fi

  selectors=$(printf '%s' "$selector_expr" | tr ',;' '  ')
  for selector in $selectors; do
    [ -n "$selector" ] || continue
    if ! resolve_selector "$selector" "$targets_tmp" >> "$selected_tmp"; then
      echo "Unknown module selector: $selector" >&2
      echo "Available module targets:" >&2
      print_aliases < "$targets_tmp" | awk -F'\t' '{ printf "  - %s (alias: %s)\n", $1, $2 }' >&2
      exit 1
    fi
  done

  if [ ! -s "$selected_tmp" ]; then
    echo "No module targets matched '$selector_expr'" >&2
    exit 1
  fi

  sort -u "$selected_tmp"
}

cmd="${1-}"
build_dir="${2-}"
selector="${3-}"

case "$cmd" in
  list)
    [ -n "$build_dir" ] || usage
    require_build_dir "$build_dir"
    list_executable_targets "$build_dir"
    ;;
  list-aliases)
    [ -n "$build_dir" ] || usage
    require_build_dir "$build_dir"
    list_executable_targets "$build_dir" | print_aliases
    ;;
  resolve)
    [ -n "$build_dir" ] || usage
    require_build_dir "$build_dir"
    resolve_targets "$build_dir" "$selector"
    ;;
  *)
    usage
    ;;
esac
