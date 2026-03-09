# Libs And Utils

## Purpose

This bucket contains low-level helpers and vendored/shared support code.

Important areas:

- `src/libs/json`
- `src/libs/jj_log`
- `src/libs/linked_list`
- `src/libs/atomic_file_rw.h`
- `src/utils/*`

## Critique

This part of the tree is mixed by nature.

- Some of it is infrastructure you own.
- Some of it is compatibility glue.
- Some of it is effectively third-party support code.

The main thing to watch is accidental architectural leakage. Utility code should stay boring. When helper layers start absorbing application policy, they become hard to reason about and even harder to test.

`atomic_file_rw.h` is the clearest example of a place to be careful. It is now a compatibility seam, which means changes there can quietly affect old call sites far away from the SDK work.

## Quality bar

For this area, the right bar is:

- minimal surprises
- explicit contracts
- no hidden policy unless absolutely necessary
- very conservative behavioral changes
