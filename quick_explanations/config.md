# Config

## Purpose

The config module is a JSON tree loader/merger/path accessor layer.

It is infrastructure code used to normalize config handling without inventing a big wrapper type system.

## Key file

- `src/config/config.c`

## Design strengths

- The subtree-based approach is pragmatic.
- Reusing `cJSON*` directly keeps the abstraction light.
- Dot-path traversal and merge utilities are useful and general.

## Main weaknesses

- This module is powerful enough that subtle behavior matters a lot.
- Merge and set-by-path logic are correctness-sensitive and easy to regress.
- Because the abstraction is intentionally thin, call sites still need discipline.

## Critique

This is a pragmatic module rather than a beautiful one, and that is okay. The design choice is basically: keep config representation simple and pay some discipline cost at call sites.

That is often the right tradeoff in C. The danger is that config semantics become distributed: some validation stays in `config.c`, some in callers, some in ad hoc `cJSON` access elsewhere.

The long-term win would be to centralize more schema validation at the boundaries without bloating the config layer itself.
