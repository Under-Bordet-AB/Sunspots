# Sunspots Bug Drop-off

Date: February 13, 2026  
Purpose: List of known and potential bugs.

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

## Compute Module (`src/compute`)

1. `[#37][Medium]` Compute manager heartbeat interval has non-positive pacing bug  
File refs: `src/compute/compute_manager.c:41`, `src/compute/compute_manager.c:44`, `src/compute/compute_manager.c:94`  
Repro: run compute manager with heartbeat arg `0` or negative; loop can signal parent continuously.

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
