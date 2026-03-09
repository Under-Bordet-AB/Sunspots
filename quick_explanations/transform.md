# Transform

## Purpose

Transform code converts raw API JSON into internal weather/forecast/price models.

This is the schema boundary between external providers and Sunspots’ internal types.

## Key files

- `src/transform/weather/openmeteo.c`
- `src/transform/price/*`
- `src/transform/*/model*.h`

## Design strengths

- Transform code is conceptually in the right place.
- It validates units and expected fields instead of silently assuming them.
- The module boundary is clear: transform should not own network or persistence.

## Main weaknesses

- These functions are still fairly branchy and repetitive.
- A lot of validation is hand-written at each call site.
- The transform layer does not yet feel standardized around reusable extraction helpers and shared validation tables.

## Critique

This layer has the right responsibility, but the implementations are still somewhat ad hoc. `transform_openmeteo_*` is a good example: it is explicit and understandable, but repetitive and vulnerable to copy-paste drift when new fields are added.

The next maturity step would be:

- stronger shared extraction helpers
- field mapping tables
- clearer separation between validation and assignment

The code is readable today, but maintenance cost will rise as more providers and metrics are added.
