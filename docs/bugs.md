# Sunspots Bug Drop-off

Date: February 13, 2026  
Purpose: Handoff list of known bugs discovered during build/test/benchmark/fuzz work.

## Critical

1. Undefined behavior and incorrect event indexing in daemon loop  
File refs: `src/core/main.c:155`, `src/core/main.c:157`, `src/core/main.c:181`  
Repro: run daemon under normal load/config change activity.

2. Runtime path model conflicts with out-of-source builds  
File refs: `src/core/main.c:620`, `src/core/main.c:32`, `src/fetch/fetch_manager.c:62`, `config/sunspots.json:11`, `config/fetch_manager_config.json:5`, `config/fetch_manager_config.json:10`  
Repro: start components from CMake build output without path patching.

## High

3. Use-after-free in fetch file loader helper  
File refs: `src/fetch/fetch_utils.h:66`, `src/fetch/fetch_utils.h:67`  
Repro: trigger partial-read failure path.

4. Frontend client queue has no full-queue handling policy  
File refs: `src/frontend/client_queue.c:14`, `src/frontend/client_queue.c:15`, `src/frontend/client_queue.c:24`  
Repro: sustained high client enqueue rate.

5. HTTP response allocation paths can null-deref  
File refs: `src/frontend/http_parser.c:387`, `src/frontend/http_parser.c:388`, `src/frontend/http_parser.c:390`, `src/frontend/http_parser.c:452`, `src/frontend/http_parser.c:454`  
Repro: memory-pressure conditions.

6. Signal handlers call non-async-signal-safe functions  
File refs: `src/fetch/fetch_manager.c:195`, `src/core/main.c:672`  
Repro: signal-heavy runtime.

7. Use-after-free in config merge path  
File refs: `src/config/config.c:104`, `src/config/config.c:116`  
Repro: `make test-unit` (fails in `config.module.test` when bug is present).

8. `config_load_env` can fail to apply env overrides and leak cJSON items  
File refs: `src/config/config.c:298`, `src/config/config.c:305`, `src/config/config.c:312`  
Repro: `make test-unit` with env override assertions.

## Medium

9. Build is not warning-clean  
File refs: `src/frontend/endpoints.c:65`, `src/fetch/fetch_manager.c:191`, `src/fetch/apis/fetch_openmeteo.c:96`, `src/fetch/apis/fetch_elprisjustnu.c:106`, `src/core/main.c:45`  
Repro: `make build`.

10. Socket family mismatch in frontend init  
File ref: `src/frontend/http_main.c:35`  
Repro: inspect/init network path.

11. Socket send path ignores partial writes  
File ref: `src/frontend/http_worker.c:123`  
Repro: busy network / large payload responses.

12. Atomic file writer not robust to partial write and missing parent dir  
File refs: `src/libs/atomic_file_rw.h:125`, `src/libs/atomic_file_rw.h:141`  
Repro: constrained IO / missing `.db/` path.

13. HTTP header lookup can dereference null pointers  
File ref: `src/frontend/http_parser.c:110`  
Repro: `make fuzz-run TARGET=http_request_fuzzer FUZZ_TIME=60` (when bug is present).

14. HTTP query allocation cleanup is incomplete on some error paths  
File refs: `src/frontend/http_parser.c`, `src/frontend/http_request_dispose` path  
Repro: `make fuzz-run TARGET=http_request_fuzzer FUZZ_TIME=60` under sanitizers.
