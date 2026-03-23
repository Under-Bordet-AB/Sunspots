# Bugs

## Frontend HTTP queue can deadlock under load

- Severity: high
- Area: `src/frontend/client_queue.c`
- Summary: `enqueue_client()` returns early when the queue is full without unlocking `q_mutex`, which can permanently block producers and consumers. The ring-buffer state also uses `q_head == q_tail` as the empty check while separately tracking fullness with `enqueues`, which is easy to misuse and already led to broken control flow.
- References:
  - `src/frontend/client_queue.c:12`
  - `src/frontend/client_queue.c:28`

## Frontend listener uses the wrong socket family

- Severity: high
- Area: `src/frontend/http_main.c`
- Summary: the code creates an IPv4 `sockaddr_in` but sets `sin_family = AF_UNSPEC` instead of `AF_INET`. That is incorrect socket setup and may fail depending on platform behavior.
- References:
  - `src/frontend/http_main.c:18`
  - `src/frontend/http_main.c:35`

## Request routing lowercases file paths

- Severity: medium
- Area: `src/frontend/endpoints.c`
- Summary: `process_request()` lowercases the incoming path before alias matching and file lookup. On case-sensitive filesystems this changes resource identity and can make valid aliases or files unreachable.
- References:
  - `src/frontend/endpoints.c:66`

## HTTP response serializer sends a trailing NUL byte

- Severity: medium
- Area: `src/frontend/http_parser.c`, `src/frontend/http_worker.c`
- Summary: `http_response_stringify()` includes space for the C-string terminator in `messageSize`, stores that full value in `outSize`, and the worker sends that full size. That means an extra NUL byte is sent on the wire for every response.
- References:
  - `src/frontend/http_parser.c:453`
  - `src/frontend/http_worker.c:121`

## Frontend runtime config relies on mutable globals

- Severity: medium
- Area: `src/frontend/http_constants.*`, `src/frontend/frontend_main.c`
- Summary: frontend configuration and runtime state are spread across mutable globals such as `HTTP_PORT`, `LISTENER_COUNT`, `QUEUE_SIZE`, `FILE_SEARCH_DIR`, and `URL_ALIASES`. This makes startup ordering, ownership, and testing harder than necessary and increases the chance of cross-module bugs.
- References:
  - `src/frontend/http_constants.h:8`
  - `src/frontend/http_constants.c:4`
  - `src/frontend/frontend_main.c:53`

## Highest-risk HTTP code has no dedicated tests

- Severity: medium
- Area: `tests/CMakeLists.txt`, `src/frontend/`
- Summary: the repo has solid unit coverage around SDK and backfill, but there are no dedicated tests for the hand-rolled HTTP parser, queue, worker loop, or endpoint routing. The subsystem with the most stateful string parsing is also the least protected against regressions.
- References:
  - `tests/CMakeLists.txt:1`
  - `src/frontend/CMakeLists.txt:1`

## Terminal client HTTP handling is brittle by design

- Severity: low
- Area: `src/client/http/*`
- Summary: the terminal client reads until socket close instead of honoring `Content-Length` or transfer semantics, and it only works cleanly because requests force `Connection: close`. This is not immediately broken for the current server pairing, but it is fragile and easy to regress if the server behavior changes.
- References:
  - `src/client/http/http_request.cpp:31`
  - `src/client/http/http_client.cpp:115`
  - `src/client/http/http_response.cpp:26`
