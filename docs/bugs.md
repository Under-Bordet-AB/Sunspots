# Sunspots Bug Drop-off

Date: February 13, 2026  
Purpose: Handoff list of known bugs discovered during build/test/benchmark/fuzz work.

This list is now sorted by module folder so each module owner can scan only their area.
Original bug IDs are preserved as `#<id>` for continuity.

## Module Ownership Hints (Git History)

- `src/core`: first commit author `timackevald@gmail.com`; top contributors `timackevald@gmail.com`, `Jimmy Jordan`, `gustavfrisen`.
- `src/frontend`: first commit author `EmK530`; top contributors `EmK530`, `Jimmy Jordan`.
- `src/fetch`: first commit author `timackevald@gmail.com`; top contributors `Gustav Frisén`, `timackevald@gmail.com`, `gustavfrisen`.
- `src/config`: first commit author `Jimmy Jordan`; top contributors `Jimmy Jordan`, `timackevald@gmail.com`.
- `src/compute`: first commit author `timackevald@gmail.com`; top contributors `Gustav Frisén`, `gustavfrisen`, `Jimmy Jordan`.
- `src/sdk`: first commit author `Jimmy Jordan`; top contributor `Jimmy Jordan`.
- `src/libs`: first commit author `timackevald@gmail.com`; top contributors `Jimmy Jordan`, `timackevald@gmail.com`, `EmK530`.
- `config/`: first commit author `Jimmy Jordan`; top contributors `timackevald@gmail.com`, `Jimmy Jordan`, `gustavfrisen`.

## Core Module (`src/core`)

1. `[#1][Critical]` Undefined behavior and incorrect event indexing in daemon loop  
File refs: `src/core/main.c:155`, `src/core/main.c:157`, `src/core/main.c:181`  
Repro: run daemon under normal load/config change activity.

2. `[#15][High]` Clang-based builds are blocked by daemon API declaration drift  
File refs: `src/core/main.c:117`, `src/core/main.c:256`, `src/core/daemon.h:96`, `src/core/daemon.h:99`  
Repro: `make fuzz-build` or `make tidy` (fails with conflicting type for `daemon_load_modules`).

3. `[#16][High]` Null-path module entries can crash reload/respawn paths  
File refs: `src/core/main.c:302`, `src/core/main.c:349`, `src/core/main.c:518`  
Repro: configure a module `bin_path` that cannot be resolved by `realpath`, then hot-reload.

4. `[#27][Medium]` Project-root resolve failure is logged but execution continues with invalid root state  
File refs: `src/core/main.c:626`, `src/core/main.c:628`, `src/core/main.c:33`  
Repro: run daemon in environment where `realpath` of computed root fails; startup proceeds to path construction with bad root buffer.

## Frontend Module (`src/frontend`)

1. `[#4][High]` Frontend client queue has no full-queue handling policy  
File refs: `src/frontend/client_queue.c:14`, `src/frontend/client_queue.c:15`, `src/frontend/client_queue.c:24`  
Repro: sustained high client enqueue rate.

2. `[#5][High]` HTTP response allocation paths can null-deref  
File refs: `src/frontend/http_parser.c:387`, `src/frontend/http_parser.c:388`, `src/frontend/http_parser.c:390`, `src/frontend/http_parser.c:452`, `src/frontend/http_parser.c:454`  
Repro: memory-pressure conditions.

3. `[#17][High]` Request object disposal leaks parsed query structures on normal path  
File refs: `src/frontend/http_parser.c:243`, `src/frontend/http_parser.c:266`, `src/frontend/http_parser.c:351`  
Repro: loop parse/dispose requests with query params under LeakSanitizer.

4. `[#18][High]` Additional request-parser OOM/error branches leak previously allocated fields  
File refs: `src/frontend/http_parser.c:246`, `src/frontend/http_parser.c:299`, `src/frontend/http_parser.c:303`  
Repro: fault-inject allocation failures in request parsing (clang static analyzer also flags this path).

5. `[#10][Medium]` Socket family mismatch in frontend init  
File ref: `src/frontend/http_main.c:35`  
Repro: inspect/init network path.

6. `[#11][Medium]` Socket send path ignores partial writes  
File ref: `src/frontend/http_worker.c:123`  
Repro: busy network / large payload responses.

7. `[#13][Medium]` HTTP header lookup can dereference null pointers  
File ref: `src/frontend/http_parser.c:110`  
Repro: `make fuzz-run TARGET=http_request_fuzzer FUZZ_TIME=60` (when bug is present).

8. `[#14][Medium]` HTTP query allocation cleanup is incomplete on some error paths  
File refs: `src/frontend/http_parser.c`, `src/frontend/http_request_dispose` path  
Repro: `make fuzz-run TARGET=http_request_fuzzer FUZZ_TIME=60` under sanitizers.

9. `[#20][Medium]` HTTP response serialization mutates headers and includes trailing NUL byte in transmitted size  
File refs: `src/frontend/http_parser.c:444`, `src/frontend/http_parser.c:450`, `src/frontend/http_parser.c:469`, `src/frontend/http_worker.c:123`  
Repro: stringify/send same response object multiple times and inspect duplicate `Content-Length` plus byte-count mismatch.

10. `[#21][Medium]` Worker request lifecycle has leak/disconnect edge cases  
File refs: `src/frontend/http_worker.c:96`, `src/frontend/http_worker.c:100`, `src/frontend/http_worker.c:112`  
Repro: trigger `process_request` failure path (request object not disposed before `break`); send `Connection: Keep-Alive` variant casing.

11. `[#22][Medium]` Frontend init can leak listening socket and join non-started workers after thread-create failures  
File refs: `src/frontend/http_main.c:51`, `src/frontend/http_main.c:55`, `src/frontend/http_main.c:66`, `src/frontend/http_main.c:103`  
Repro: fault-inject `calloc`/`pthread_create` failures.

12. `[#25][Medium]` Frontend heartbeat arg parsing is weak and can trigger signal flood behavior  
File refs: `src/frontend/frontend_main.c:42`, `src/frontend/frontend_main.c:49`, `src/frontend/frontend_main.c:85`  
Repro: pass malformed or non-positive heartbeat values; missing endptr/errno reset/range validation allows unstable cadence logic.

13. `[#26][Medium]` Path sanitation overflow check is ineffective with truncated `snprintf` output  
File refs: `src/frontend/endpoints.c:70`, `src/frontend/endpoints.c:73`  
Repro: very long URL path; `snprintf` truncates into `temp`, `strlen(temp)` no longer detects original overflow.

## Fetch Module (`src/fetch`)

1. `[#3][High]` Use-after-free in fetch file loader helper  
File refs: `src/fetch/fetch_utils.h:66`, `src/fetch/fetch_utils.h:67`  
Repro: trigger partial-read failure path.

2. `[#19][High]` Fetch APIs ignore normalized payload and persist raw API response instead  
File refs: `src/fetch/apis/fetch_openmeteo.c:58`, `src/fetch/apis/fetch_openmeteo.c:65`, `src/fetch/apis/fetch_elprisjustnu.c:66`, `src/fetch/apis/fetch_elprisjustnu.c:73`  
Repro: instrument `normalize_data` to transform output; DB writes still use unnormalized `buffer`.

3. `[#23][Medium]` Fetch manager parses per-API `interval` but never uses it for scheduling  
File refs: `src/fetch/fetch_manager.c:22`, `src/fetch/fetch_manager.c:164`, `src/fetch/fetch_manager.c:115`  
Repro: set different `interval` values in `config/fetch_manager_config.json`; runtime loop remains fixed at `sleep(2)`.

4. `[#36][Medium]` Fetch manager heartbeat interval accepts non-positive values, enabling tight signal/log loop  
File refs: `src/fetch/fetch_manager.c:56`, `src/fetch/fetch_manager.c:57`, `src/fetch/fetch_manager.c:185`  
Repro: start fetch manager with heartbeat arg `0` or negative; `sleep(g_heartbeat_freq)` no longer provides safe pacing.

5. `[#41][High]` `fetch_manager_config_fuzzer` cannot link under libFuzzer configuration  
File refs: `src/fetch/fetch_manager.c:38`, `fuzz/fetch_manager_config_fuzzer.cpp:38`, `fuzz/CMakeLists.txt:58`  
Repro: `make fuzz-build FUZZ_ENGINE=libfuzzer` (reports multiple `main` definition plus missing `fetch_manager_reset_apis` symbol).

## Config Module (`src/config` and `config/`)

1. `[#7][High]` Use-after-free in config merge path  
File refs: `src/config/config.c:104`, `src/config/config.c:116`  
Repro: `make test-unit` (fails in `config.module.test` when bug is present).

2. `[#8][High]` `config_load_env` can fail to apply env overrides and leak cJSON items  
File refs: `src/config/config.c:298`, `src/config/config.c:305`, `src/config/config.c:312`  
Repro: `make test-unit` with env override assertions.

## Compute Module (`src/compute`)

1. `[#37][Medium]` Compute manager heartbeat interval has non-positive pacing bug  
File refs: `src/compute/compute_manager.c:41`, `src/compute/compute_manager.c:44`, `src/compute/compute_manager.c:94`  
Repro: run compute manager with heartbeat arg `0` or negative; loop can signal parent continuously.

## SDK Module (`src/sdk`)

1. `[#28][High]` SDK DB dedupe key is too weak and silently drops distinct records  
File refs: `src/sdk/internal/db/ss_db_internal.c:422`, `src/sdk/internal/db/ss_db_internal.c:425`, `src/sdk/internal/db/ss_db_internal.c:427`, `src/sdk/internal/db/ss_db_internal.c:429`, `src/sdk/internal/db/ss_db_internal.c:571`, `src/sdk/internal/db/ss_db_internal.c:613`  
Repro: write two observation records with same `metric + ts_start + source_api` but different `value`, `source_field`, and `ts_end`; second write returns success but row is not persisted.

2. `[#29][High]` SDK allows non-finite numeric values (NaN/Inf) to pass validation and persist  
File refs: `src/sdk/ss_sdk.c:37`, `src/sdk/ss_sdk.c:72`, `src/sdk/internal/db/ss_db_internal.c:452`, `src/sdk/internal/db/ss_db_internal.c:300`  
Repro: write a record with `value.f64 = NAN`; `ss_sdk_db_write_record` returns `SS_SDK_OK` and DB row contains `nan`.

3. `[#24][Medium]` SDK write loops can spin forever if `write(2)` returns 0  
File refs: `src/sdk/internal/log/ss_log_internal.c:81`, `src/sdk/internal/log/ss_log_internal.c:82`, `src/sdk/internal/db/ss_db_internal.c:132`, `src/sdk/internal/db/ss_db_internal.c:133`  
Repro: fault-inject short/zero write behavior; loops do not advance `off` on `nw == 0`.

4. `[#30][Medium]` Logger silently drops events when `SUNSPOTS_CONFIG` is malformed  
File refs: `src/sdk/internal/log/ss_log_internal.c:207`, `src/sdk/internal/log/ss_log_internal.c:208`, `src/sdk/internal/log/ss_log_internal.c:224`, `src/sdk/internal/log/ss_log_internal.c:225`  
Repro: set `SUNSPOTS_CONFIG='{"log_path":123}'`; `ss_sdk_log_write_auto(...)` returns `SS_SDK_OK` but no log is emitted.

5. `[#31][Medium]` `log_path` extraction truncates long paths silently instead of erroring  
File refs: `src/sdk/internal/log/ss_log_internal.c:188`, `src/sdk/internal/log/ss_log_internal.c:189`, `src/sdk/internal/log/ss_log_internal.c:193`, `src/sdk/internal/log/ss_log_internal.c:217`  
Repro: provide `log_path` longer than logger path buffer (1024); truncated path is used with no explicit error.

6. `[#32][Medium]` Logger timestamp path uses thread-unsafe `gmtime`  
File refs: `src/sdk/internal/log/ss_log_internal.c:288`, `src/sdk/internal/log/ss_log_internal.c:292`  
Repro: concurrent high-volume logging from multiple threads can race on libc static time buffer.

7. `[#33][Medium]` DB reader accepts out-of-range enum fields from disk without canonical validation  
File refs: `src/sdk/internal/db/ss_db_internal.c:329`, `src/sdk/internal/db/ss_db_internal.c:334`, `src/sdk/internal/db/ss_db_internal.c:349`, `src/sdk/internal/db/ss_db_internal.c:707`  
Repro: inject corrupted DB rows with invalid `metric/value_type/data_kind`; reader may return structurally invalid records instead of rejecting them.

8. `[#34][Medium]` Escape-size calculations in SDK DB/log paths have no overflow guards  
File refs: `src/sdk/internal/db/ss_db_internal.c:148`, `src/sdk/internal/db/ss_db_internal.c:160`, `src/sdk/internal/log/ss_log_internal.c:97`, `src/sdk/internal/log/ss_log_internal.c:108`  
Repro: extremely large untrusted strings can overflow size accounting before allocation.

9. `[#35][Medium]` SDK write paths acknowledge success without forcing durability to disk  
File refs: `src/sdk/internal/db/ss_db_internal.c:627`, `src/sdk/internal/log/ss_log_internal.c:243`, `src/sdk/internal/log/ss_log_internal.c:249`  
Repro: force abrupt process/host crash after successful write call; recently acknowledged DB/log entries can be lost.

10. `[#38][Medium]` SDK float serialization/parsing is locale-dependent and can break portability  
File refs: `src/sdk/internal/db/ss_db_internal.c:452`, `src/sdk/internal/db/ss_db_internal.c:300`  
Repro: write records under one locale and read under another locale with different decimal separator rules.

11. `[#39][Medium]` Log-path key extraction uses naive substring search and can match wrong JSON context  
File refs: `src/sdk/internal/log/ss_log_internal.c:143`, `src/sdk/internal/log/ss_log_internal.c:153`, `src/sdk/internal/log/ss_log_internal.c:159`  
Repro: place `"log_path"` token inside unrelated JSON string/value before actual config key; parser may bind to unintended position.

12. `[#40][Medium]` Log-path extraction copies raw JSON string bytes without full unescape semantics  
File refs: `src/sdk/internal/log/ss_log_internal.c:176`, `src/sdk/internal/log/ss_log_internal.c:193`, `src/sdk/internal/log/ss_log_internal.c:194`  
Repro: provide escaped path sequences in JSON (for example escaped slashes/backslashes); resulting filesystem path can differ from intended decoded JSON value.

## Libraries Module (`src/libs`)

1. `[#12][Medium]` Atomic file writer not robust to partial write and missing parent dir  
File refs: `src/libs/atomic_file_rw.h:125`, `src/libs/atomic_file_rw.h:141`  
Repro: constrained IO / missing `.db/` path.

## Cross-Module / System-Wide

1. `[#2][Critical]` Runtime path model conflicts with out-of-source builds  
File refs: `src/core/main.c:620`, `src/core/main.c:32`, `src/fetch/fetch_manager.c:62`, `config/sunspots.json:11`, `config/fetch_manager_config.json:5`, `config/fetch_manager_config.json:10`  
Repro: start components from CMake build output without path patching.

2. `[#6][High]` Signal handlers call non-async-signal-safe functions  
File refs: `src/fetch/fetch_manager.c:195`, `src/core/main.c:672`  
Repro: signal-heavy runtime.

3. `[#9][Medium]` Build is not warning-clean across modules  
File refs: `src/frontend/endpoints.c:65`, `src/fetch/fetch_manager.c:191`, `src/fetch/apis/fetch_openmeteo.c:96`, `src/fetch/apis/fetch_elprisjustnu.c:106`, `src/core/main.c:45`  
Repro: `make build`.

## Status Snapshot (Current Branch)

### SDK Module (`src/sdk`)

- `#24` Fixed in current branch (`write(...) == 0` now returns error in DB/log write loops).
- `#28` Fixed in current branch (dedupe identity now uses full canonical tuple + value payload).
- `#29` Fixed in current branch (`isfinite` validation added for `SS_SDK_VALUE_F64`).
- `#30` Fixed in current branch (malformed `SUNSPOTS_CONFIG` now returns internal error).
- `#31` Fixed in current branch (overlong `log_path` now errors instead of truncating).
- `#32` Fixed in current branch (`gmtime_r` replaces `gmtime`).
- `#33` Fixed in current branch (read path validates metric/value_type/data_kind enums).
- `#34` Fixed in current branch (overflow-checked escape size accounting in DB/log paths).
- `#35` Fixed in current branch (DB/log writes call `fsync` before returning success).
- `#38` Fixed in current branch (F64 persisted as locale-stable hex bits, parser supports legacy decimal fallback).
- `#39` Fixed in current branch (`log_path` extraction uses cJSON object parsing).
- `#40` Fixed in current branch (JSON string unescape semantics handled by cJSON parser).

### Libraries Module (`src/libs`)

- `#12` Fixed in current branch (`af_save` now ensures parent dirs and uses write-all loop with short-write guards).

### Validation Notes

- Scoped regression passed:
  - `ctest --test-dir build/debug -L component:sdk`
  - `ctest --test-dir build/debug -L component:libs`
- Full `make test` is still blocked by existing config bug `#7` (ASan use-after-free in `config.module.test`).
