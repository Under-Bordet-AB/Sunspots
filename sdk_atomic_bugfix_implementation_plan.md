# SDK + Atomic File Backend Bugfix Implementation Plan

## 1. Goal

Before introducing the new DB API layer, fix all SDK and `atomic_file_rw` bugs listed in `docs/bugs.md` so the current file-backed MVP is correct, deterministic, and safe for low write-rate production use.

Scope is intentionally limited to:
- SDK module bugs: `#24`, `#28`-`#35`, `#38`-`#40`
- Atomic file writer bug: `#12`

Out of scope for this plan:
- SQLite/Postgres backend implementation
- New ergonomic API wrappers
- Cross-module bugs not listed above

## 2. Constraints and Decisions

1. Keep file backend for MVP.
2. Keep all DB calls blocking.
3. Keep public SDK surface stable during bugfix phase.
4. Prefer internal refactors over public API changes.
5. Preserve compatibility path for future `atomic_file_rw` -> SDK adapter.
6. Develop test-first: write or update failing tests before each bugfix implementation.

## 3. Exit Criteria

This phase is complete when all below are true:

1. All scoped bugs are fixed with tests.
2. `make build` and `make test` pass.
3. New SDK/atomic regression tests pass under ASan/UBSan.
4. SDK DB benchmark suite runs and baseline metrics are recorded.
5. SDK DB fuzz suite runs with no sanitizer findings in configured runtime budget.
6. `docs/bugs.md` entries in this scope are marked resolved or linked to follow-up issue IDs.
7. No breaking changes to current public SDK function signatures.

## 4. Work Breakdown (by fix stream)

## Stream 0: Quality Harness Bootstrap (Do First)

Purpose:
- Build the minimum test/benchmark/fuzz harness before core bugfixes so implementation work is guided by objective checks.

Tasks:
1. Add dedicated SDK DB unit/regression test files.
2. Add at least one SDK DB benchmark target.
3. Add at least one SDK DB fuzz target (file-backed parser/write/read paths).
4. Add deterministic test fixtures under `tests/fixtures/` for valid and corrupted DB rows.
5. Wire targets into CMake/Make workflows.

Files (expected):
- `tests/unit/sdk_db_*.cpp`
- `benchmarks/sdk_db_benchmark.cpp`
- `fuzz/sdk_db_*.cpp`
- `tests/fixtures/sdk_db/*`

Minimum harness gate:
1. Unit tests compile and fail for at least one known bug before fix.
2. Benchmark target runs and prints baseline numbers.
3. Fuzz target runs for short smoke interval without harness crashes.

## Stream A: SDK DB Correctness (Highest Priority)

Bugs: `#28`, `#29`, `#33`, `#38`

### A1. Fix weak dedupe identity (`#28`)

Tasks:
1. Replace current duplicate check key with full canonical identity tuple.
2. Ensure fields that differ in valid records (value/source_field/ts_end/etc) do not dedupe away real data.
3. Keep dedupe deterministic and explicit.

Files:
- `src/sdk/internal/db/ss_db_internal.c`

Tests:
1. Write two records that share metric/ts_start/source_api but differ in other fields; expect count `2`.
2. Write exact duplicate record twice; expect count `1`.

### A2. Reject non-finite floating point values (`#29`)

Tasks:
1. Add validation for `isfinite(rec->value.f64)` when `value_type == SS_SDK_VALUE_F64`.
2. Reject non-finite values before serialization.

Files:
- `src/sdk/ss_sdk.c`

Tests:
1. `NAN`, `INFINITY`, `-INFINITY` writes return validation error.
2. Normal finite values still pass.

### A3. Validate parsed enum ranges on read path (`#33`)

Tasks:
1. Validate parsed `metric`, `value_type`, and `data_kind` after parsing DB line.
2. Reject corrupted rows safely.

Files:
- `src/sdk/internal/db/ss_db_internal.c`

Tests:
1. Corrupted DB file with invalid enum integers does not produce invalid output records.
2. Read call handles mixed valid/corrupt rows deterministically.

### A4. Make float encoding/decoding locale-stable (`#38`)

Tasks:
1. Remove locale-sensitive behavior from float persistence format.
2. Use locale-invariant representation for DB storage and parse path.

Files:
- `src/sdk/internal/db/ss_db_internal.c`

Tests:
1. Round-trip F64 under non-`C` locale.
2. Verify parse/write consistency for representative values.

## Stream B: SDK Logging and Config Parsing

Bugs: `#30`, `#31`, `#32`, `#39`, `#40`

### B1. Replace naive log_path extraction (`#39`, `#40`)

Tasks:
1. Replace substring-based parser with proper JSON parse logic.
2. Decode escaped JSON strings correctly.
3. Reject malformed structures explicitly.

Files:
- `src/sdk/internal/log/ss_log_internal.c`

Tests:
1. Valid JSON with escaped path decodes correctly.
2. Token-in-string false positives are eliminated.
3. Malformed JSON returns error, not silent success.

### B2. Stop silent log drops on invalid config (`#30`)

Tasks:
1. Distinguish "logging disabled" from "invalid logging config".
2. Return explicit internal error for malformed config.

Files:
- `src/sdk/internal/log/ss_log_internal.c`

Tests:
1. `SUNSPOTS_CONFIG='{"log_path":123}'` returns error.
2. Missing log path still behaves as intentional no-op.

### B3. Handle long log paths explicitly (`#31`)

Tasks:
1. Return explicit error on path overflow/truncation risk.
2. Do not silently truncate paths.

Files:
- `src/sdk/internal/log/ss_log_internal.c`

Tests:
1. Overlong path fails with deterministic error.

### B4. Use thread-safe time conversion (`#32`)

Tasks:
1. Replace `gmtime` with `gmtime_r`.

Files:
- `src/sdk/internal/log/ss_log_internal.c`

Tests:
1. Multi-thread logging smoke test with no data races in sanitizer run.

## Stream C: I/O Safety and Durability Semantics

Bugs: `#24`, `#34`, `#35`, `#12`

### C1. Fix zero-write infinite loop risk (`#24`)

Tasks:
1. Update write loops to treat `write(...) == 0` as error.
2. Prevent tight/infinite loops.

Files:
- `src/sdk/internal/db/ss_db_internal.c`
- `src/sdk/internal/log/ss_log_internal.c`

Tests:
1. Fault injection for zero-write path returns error without hang.

### C2. Add overflow guards for escape-size accounting (`#34`)

Tasks:
1. Add checked arithmetic for output length growth in escape helpers.
2. Fail safely on size overflow.

Files:
- `src/sdk/internal/db/ss_db_internal.c`
- `src/sdk/internal/log/ss_log_internal.c`

Tests:
1. Large-input fuzz/synthetic overflow tests return errors safely.

### C3. Implement explicit durability behavior (`#35`)

Tasks:
1. Add internal durability mode for file-backed writes (`best effort` vs `flush`).
2. Default to current behavior to avoid surprise performance regressions.
3. Provide hook for future API exposure.

Files:
- `src/sdk/internal/db/ss_db_internal.c`
- `src/sdk/internal/log/ss_log_internal.c`

Tests:
1. Unit/integration check that flush mode calls sync path successfully.

### C4. Harden atomic file writer (`#12`)

Tasks:
1. Ensure parent directory exists before open.
2. Replace single-write assumption with write-all loop.
3. Handle partial write and short-write errors correctly.

Files:
- `src/libs/atomic_file_rw.h`

Tests:
1. Missing parent path is created or returns deterministic error based on policy.
2. Partial-write fault injection path does not report false success.

## 5. Execution Order (Critical Path)

1. Stream 0 first (quality harness bootstrap).
2. Stream A (DB correctness).
3. Stream C1/C2 (I/O safety).
4. Stream B (logging correctness).
5. Stream C3/C4 (durability and atomic writer hardening).
6. Final full regression: tests + benchmark + fuzz + docs update.

Rationale:
- Test-first reduces rework and catches regressions during large storage refactors.
- Preventing data loss/corruption is highest priority before behavior ergonomics.

## 6. Testing Plan

## 6.1 New tests to add

1. `tests/unit/sdk_db_dedupe_test.cpp`.
2. `tests/unit/sdk_db_validation_test.cpp` (finite checks + enum checks).
3. `tests/unit/sdk_log_config_test.cpp`.
4. `tests/unit/sdk_io_fault_test.cpp` (zero-write/partial-write simulation).
5. `tests/unit/atomic_file_rw_test.cpp`.
6. `tests/unit/sdk_db_locale_test.cpp` (locale-stable float roundtrip).
7. `tests/unit/sdk_db_corruption_test.cpp` (invalid rows / enum bounds).

## 6.2 Test modes

1. Normal unit run.
2. ASan/UBSan run.
3. Locale-variant run for float roundtrip (`LC_NUMERIC`).
4. Stress/concurrency smoke test for logging path.

## 6.3 Regression command set

1. `make build`
2. `make test`
3. Focused SDK tests (label or test-name filter once added)

## 6.4 Benchmark plan (new)

Goals:
1. Detect performance regressions while fixing correctness.
2. Record MVP baseline for file backend before API redesign.

Targets:
1. `benchmarks/sdk_db_benchmark.cpp` with workloads:
- single write latency
- batched read latency
- dedupe-path write latency

Run policy:
1. Run benchmark before first bugfix commit (baseline).
2. Run benchmark after Stream A and after final integration.
3. Store results in `build/ci-logs/` and summarize in PR notes.

## 6.5 Fuzz plan (new)

Goals:
1. Shake parser/serialization edge cases.
2. Catch crashers and sanitizer issues in DB/log paths.

Targets:
1. DB row parser fuzz target (corrupted line inputs).
2. Log config parser fuzz target (`SUNSPOTS_CONFIG` JSON snippets).
3. Optional write/read roundtrip fuzz target with constrained record generation.

Run policy:
1. Smoke: 30s per target during iteration.
2. Gate: 5m per target before merge (or team-agreed budget).
3. Save artifacts and repro inputs under `build/fuzz-artifacts/`.

## 7. Documentation Updates

After implementation:

1. Update `docs/bugs.md` scoped entries with status and commit refs.
2. Update SDK usage docs if any behavior changed (especially logging error semantics).
3. Keep `database SDK API design.md` aligned with fixed behavior and remaining gaps.

## 8. Risk Register

1. Dedupe logic change can increase stored row count.
2. Stricter validation can reject data previously accepted.
3. Durability flush mode can slightly increase latency.

Mitigations:
1. Ship with explicit release notes.
2. Keep default behavior conservative.
3. Add test vectors for backward compatibility and migration.

## 9. Deliverables

1. Code fixes for all scoped bug IDs.
2. Added regression tests.
3. Added SDK DB benchmark target and baseline report.
4. Added SDK DB fuzz targets and runtime report.
5. Updated `docs/bugs.md` status.
6. Short changelog entry summarizing behavior changes.

## 10. Next Step After This Plan

Once this bugfix phase is complete:
1. Start implementing the new low-level DB API layer from `database SDK API design.md`.
2. Keep file backend as first implementation.
3. Add `atomic_file_rw` adapter layer on top of SDK storage.
