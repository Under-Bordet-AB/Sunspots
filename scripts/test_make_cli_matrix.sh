#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAKE_BIN="${MAKE_BIN:-make}"
MODULE_HELPER="$ROOT_DIR/scripts/module_targets.sh"

RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-8}"
VALGRIND_RUN_SECONDS="${VALGRIND_RUN_SECONDS:-3}"
INCLUDE_HEAVY="${INCLUDE_HEAVY:-1}"
INCLUDE_RUN_TARGETS="${INCLUDE_RUN_TARGETS:-1}"
ALLOW_TEST_FAILURES="${ALLOW_TEST_FAILURES:-1}"
ALLOW_RUNTIME_FAILURES="${ALLOW_RUNTIME_FAILURES:-1}"

declare -a MAKE_BASE
MAKE_BASE=("$MAKE_BIN" -C "$ROOT_DIR" --no-print-directory)

case_no=0
pass_count=0
fail_count=0
skip_count=0
declare -a failures

timeout_cmd=""
if command -v timeout >/dev/null 2>&1; then
  timeout_cmd="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
  timeout_cmd="gtimeout"
fi

run_case() {
  local name="$1"
  local expected_csv="$2"
  shift 2
  local -a cmd
  cmd=("$@")

  case_no=$((case_no + 1))
  printf "\n[%03d] %s\n" "$case_no" "$name"
  printf "      +"
  printf " %q" "${cmd[@]}"
  printf "\n"

  "${cmd[@]}"
  local rc=$?

  local ok=0
  IFS=',' read -r -a expected_codes <<< "$expected_csv"
  for expected_rc in "${expected_codes[@]}"; do
    if [ "$rc" = "$expected_rc" ]; then
      ok=1
      break
    fi
  done

  if [ "$ok" -eq 1 ]; then
    printf "      [ok] rc=%s\n" "$rc"
    pass_count=$((pass_count + 1))
  else
    printf "      [fail] rc=%s expected={%s}\n" "$rc" "$expected_csv"
    fail_count=$((fail_count + 1))
    failures+=("$name (rc=$rc expected={$expected_csv})")
  fi
}

skip_case() {
  local name="$1"
  local reason="$2"
  case_no=$((case_no + 1))
  printf "\n[%03d] %s\n" "$case_no" "$name"
  printf "      [skip] %s\n" "$reason"
  skip_count=$((skip_count + 1))
}

to_csv() {
  local IFS=,
  printf "%s" "$*"
}

if [ ! -x "$MODULE_HELPER" ]; then
  echo "Missing helper: $MODULE_HELPER" >&2
  exit 1
fi

printf "Sunspots Make CLI matrix test\n"
printf "Repo: %s\n" "$ROOT_DIR"
printf "RUN_TIMEOUT_SECONDS=%s VALGRIND_RUN_SECONDS=%s INCLUDE_HEAVY=%s INCLUDE_RUN_TARGETS=%s ALLOW_TEST_FAILURES=%s ALLOW_RUNTIME_FAILURES=%s\n" \
  "$RUN_TIMEOUT_SECONDS" "$VALGRIND_RUN_SECONDS" "$INCLUDE_HEAVY" "$INCLUDE_RUN_TARGETS" "$ALLOW_TEST_FAILURES" "$ALLOW_RUNTIME_FAILURES"

run_case "deepclean (initial)" "0" "${MAKE_BASE[@]}" deepclean
run_case "help" "0" "${MAKE_BASE[@]}" help
run_case "list-modules" "0" "${MAKE_BASE[@]}" list-modules
run_case "list-modules-valgrind" "0" "${MAKE_BASE[@]}" list-modules-valgrind

mapfile -t all_modules < <("$MODULE_HELPER" list "$ROOT_DIR/build/debug")
if [ "${#all_modules[@]}" -eq 0 ]; then
  echo "No modules discovered in build/debug" >&2
  exit 1
fi

mapfile -t test_modules < <(printf '%s\n' "${all_modules[@]}" | awk '/_test$/')
mapfile -t runtime_modules < <(printf '%s\n' "${all_modules[@]}" | awk '!/_test$/')

all_modules_csv="$(to_csv "${all_modules[@]}")"
test_modules_csv="$(to_csv "${test_modules[@]}")"
runtime_modules_csv="$(to_csv "${runtime_modules[@]}")"

test_expected_rc="0"
all_expected_rc="0"
if [ "$ALLOW_TEST_FAILURES" = "1" ]; then
  test_expected_rc="0,2"
  all_expected_rc="0,2"
fi

run_expected_rc="0,124"
if [ "$ALLOW_RUNTIME_FAILURES" = "1" ]; then
  run_expected_rc="0,2,124"
fi

printf "\nDiscovered %s module targets\n" "${#all_modules[@]}"
printf "  %s\n" "${all_modules[@]}"
printf "\nDiscovered %s test module targets\n" "${#test_modules[@]}"
if [ "${#test_modules[@]}" -gt 0 ]; then
  printf "  %s\n" "${test_modules[@]}"
fi
printf "\nDiscovered %s runtime module targets\n" "${#runtime_modules[@]}"
if [ "${#runtime_modules[@]}" -gt 0 ]; then
  printf "  %s\n" "${runtime_modules[@]}"
fi

run_case "build (all)" "0" "${MAKE_BASE[@]}" build
run_case "build-valgrind (all)" "0" "${MAKE_BASE[@]}" build-valgrind
run_case "build-tests (all)" "0" "${MAKE_BASE[@]}" build-tests
run_case "build-tests-valgrind (all)" "0" "${MAKE_BASE[@]}" build-tests-valgrind

for module in "${all_modules[@]}"; do
  run_case "build M=$module" "0" "${MAKE_BASE[@]}" build "M=$module"
  run_case "build-valgrind M=$module" "0" "${MAKE_BASE[@]}" build-valgrind "M=$module"
done

run_case "build M=<all-modules-combined>" "0" "${MAKE_BASE[@]}" build "M=$all_modules_csv"
run_case "build-valgrind M=<all-modules-combined>" "0" "${MAKE_BASE[@]}" build-valgrind "M=$all_modules_csv"

if [ "${#test_modules[@]}" -gt 0 ]; then
  for test_module in "${test_modules[@]}"; do
    run_case "build-tests M=$test_module" "0" "${MAKE_BASE[@]}" build-tests "M=$test_module"
    run_case "build-tests-valgrind M=$test_module" "0" "${MAKE_BASE[@]}" build-tests-valgrind "M=$test_module"
    run_case "run-tests M=$test_module" "$test_expected_rc" "${MAKE_BASE[@]}" run-tests "M=$test_module"
    run_case "run-tests-valgrind M=$test_module" "$test_expected_rc" "${MAKE_BASE[@]}" run-tests-valgrind "M=$test_module"
  done

  run_case "build-tests M=<all-test-modules-combined>" "0" "${MAKE_BASE[@]}" build-tests "M=$test_modules_csv"
  run_case "build-tests-valgrind M=<all-test-modules-combined>" "0" "${MAKE_BASE[@]}" build-tests-valgrind "M=$test_modules_csv"
  run_case "run-tests M=<all-test-modules-combined>" "$test_expected_rc" "${MAKE_BASE[@]}" run-tests "M=$test_modules_csv"
  run_case "run-tests-valgrind M=<all-test-modules-combined>" "$test_expected_rc" "${MAKE_BASE[@]}" run-tests-valgrind "M=$test_modules_csv"
fi

if [ "$INCLUDE_RUN_TARGETS" = "1" ]; then
  if [ -n "$timeout_cmd" ]; then
    for module in "${runtime_modules[@]}"; do
      run_case "run M=$module (timeout)" "$run_expected_rc" \
        "$timeout_cmd" "${RUN_TIMEOUT_SECONDS}s" "${MAKE_BASE[@]}" run "M=$module"
      run_case "run-valgrind M=$module (timeout)" "$run_expected_rc" \
        "$timeout_cmd" "$((VALGRIND_RUN_SECONDS + 8))s" \
        "${MAKE_BASE[@]}" run-valgrind "M=$module" "VALGRIND_RUN_SECONDS=$VALGRIND_RUN_SECONDS"
    done

    # Combined M for run targets currently resolves multiple binaries and exits non-zero by design.
    run_case "run M=<all-modules-combined> (expected non-zero)" "2" \
      "${MAKE_BASE[@]}" run "M=$runtime_modules_csv"
    run_case "run-valgrind M=<all-modules-combined> (expected non-zero)" "2" \
      "${MAKE_BASE[@]}" run-valgrind "M=$runtime_modules_csv" "VALGRIND_RUN_SECONDS=$VALGRIND_RUN_SECONDS"
  else
    skip_case "run target matrix" "timeout utility not found (need timeout or gtimeout)"
  fi
else
  skip_case "run target matrix" "INCLUDE_RUN_TARGETS=0"
fi

run_case "cppcheck" "0" "${MAKE_BASE[@]}" cppcheck
run_case "lizard" "0" "${MAKE_BASE[@]}" lizard
run_case "tidy" "0" "${MAKE_BASE[@]}" tidy

if [ "$INCLUDE_HEAVY" = "1" ]; then
  run_case "warnings" "0" "${MAKE_BASE[@]}" warnings
  run_case "all" "$all_expected_rc" "${MAKE_BASE[@]}" all
else
  skip_case "warnings" "INCLUDE_HEAVY=0"
  skip_case "all" "INCLUDE_HEAVY=0"
fi

# Placeholder targets are expected to fail (currently exit 2 by design).
run_case "e2e (expected placeholder failure)" "2" "${MAKE_BASE[@]}" e2e
run_case "e2e-valgrind (expected placeholder failure)" "2" "${MAKE_BASE[@]}" e2e-valgrind

run_case "clean" "0" "${MAKE_BASE[@]}" clean
run_case "deepclean (final)" "0" "${MAKE_BASE[@]}" deepclean

printf "\nSummary: pass=%s fail=%s skip=%s total=%s\n" \
  "$pass_count" "$fail_count" "$skip_count" "$case_no"

if [ "$fail_count" -gt 0 ]; then
  printf "Failures:\n"
  printf "  - %s\n" "${failures[@]}"
  exit 1
fi

exit 0
