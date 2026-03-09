# Client

## Purpose

The client module is the interactive/user-facing local client side.

It appears to provide:

- HTTP request building
- menu/UI flows
- planning service interaction
- input helpers

## Key files

- `src/client/http/*`
- `src/client/menu/*`
- `src/client/plan_service/*`
- `src/client/utils/*`

## Design strengths

- Responsibilities are sensibly subfoldered.
- C++ is a reasonable choice here because the client code benefits from strings, collections, and a less manual style.

## Main weaknesses

- The client side looks thinner and less battle-hardened than core/runtime code.
- There are hints of convenience-style implementation where stronger interfaces would help.
- Some classes/utilities likely need another pass on explicitness and API boundaries.

## Critique

This part of the repo feels more like an application layer than infrastructure. That is not a criticism by itself, but it means code quality pressure here is different: readability and maintainable interfaces matter more than micro-control.

The client code probably benefits the most from:

- stronger API contracts
- better separation between transport and UI logic
- clearer model boundaries for request/response payloads
