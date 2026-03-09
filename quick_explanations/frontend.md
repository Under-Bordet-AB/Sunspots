# Frontend

## Purpose

The frontend is the HTTP-facing server side of Sunspots.

It appears to serve files, endpoints, and queue/worker behavior around incoming client requests.

## Key files

- `src/frontend/frontend_main.c`
- `src/frontend/http_main.c`
- `src/frontend/http_parser.c`
- `src/frontend/http_worker.c`
- `src/frontend/endpoints.c`

## Design strengths

- Clear separation between parsing, worker handling, and top-level startup.
- Operational role is obvious: accept requests, parse them, route them, respond.

## Main weaknesses

- This area is one of the more complexity-heavy parts of the codebase.
- Manual HTTP parsing is intrinsically bug-prone.
- Global mutable config/state still shows through in places.

## Critique

The frontend looks like hand-built systems code rather than a library-backed server. That gives control, but it also means a lot of protocol and lifecycle behavior is owned locally.

That is fine if the goal is a lightweight custom server, but it means:

- parser correctness matters a lot
- request lifecycle bugs are expensive
- concurrency behavior needs tight discipline

This is an area where complexity is structural, not just accidental.
