# Fetch

## Purpose

Fetch modules talk to upstream APIs and turn responses into either SDK writes or transform-model structures.

The main pattern is:

1. read config/env
2. build URL
3. fetch JSON
4. normalize/transform
5. persist

## Key files

- `src/fetch/fetch_utils.h`
- `src/fetch/apis/fetch_openmeteo_forecast.c`

## Design strengths

- The fetchers are operationally simple.
- The happy path is easy to follow.
- Transform logic is mostly kept out of raw fetching code, which is the right idea.

## Main weaknesses

- A lot of fetch code is still bespoke rather than sharing one internal fetch-module skeleton.
- Error handling is mostly logging plus `-1`, which is easy to wire but hard to reason about at scale.
- Some modules still parse env/config by hand with `cJSON` instead of using one consistent config helper layer.

## Critique

The fetch layer feels older than the SDK/backfill work. It works, but it is not yet built around reusable module conventions. The core problem is inconsistency: each fetcher tends to do its own config parsing and persistence behavior.

If this area grows, it should move toward:

- one shared fetch module bootstrap
- one shared config-reader pattern
- one shared retry/rate-limit abstraction
- one shared persistence contract

Right now it is serviceable, but not yet a clean platform.
