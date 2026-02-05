# SUNSPOTS Coding Standard

> Target: Linux C99
> Goal: keep generated code consistent, safe, and easy to review.

## 1) Mandatory Rules

1. **Use project logging only**: runtime output goes through `src/log/sunspots_log.*`.
2. **Never use banned identifiers** listed in `docs/standards/banned.md`.
3. **Validate inputs at API boundaries** and fail early on invalid pointers/values.
4. **Return style**:
   - Internal functions: `0` success, negative errno on failure (`-EINVAL`, `-ENOMEM`, ...).
   - Pointer-returning creators: return `NULL` on failure.
5. **Ownership must be explicit**:
   - Heap ownership: `create/destroy`.
   - In-place setup: `init/fini`.
   - External resources: `open/close`.
6. **`destroy(T** p)` pattern for owned heap objects**: free internals, free object, set caller pointer to `NULL`.
7. **Use `const` for read-only pointer inputs**.
8. **Internal symbols are `static` by default** unless part of a public header API.

## 2) Naming Rules

- Use `snake_case` for variables, functions, files, and struct names.
- Use `module_action` for function names (example: `config_load`, `cache_put`).
- Use positive boolean names (`is_ready`, `has_data`, `can_retry`).
- File naming:
  - `module_name.c` implementation
  - `module_name.h` public API
  - `module_name_internal.h` internal shared declarations

## 3) Function and Error Pattern

Prefer this shape for functions that allocate multiple resources:

```c
int module_do_work(const input* in, output** out) {
    int ret = 0;
    temp_buf* buf = NULL;
    output* result = NULL;

    if (!in || !out) {
        return -EINVAL;
    }
    *out = NULL;

    buf = temp_buf_create();
    if (!buf) {
        ret = -ENOMEM;
        goto cleanup;
    }

    result = output_create();
    if (!result) {
        ret = -ENOMEM;
        goto cleanup;
    }

    ret = output_build(result, in, buf);
    if (ret != 0) {
        goto cleanup;
    }

    *out = result;
    result = NULL;

cleanup:
    output_destroy(&result);
    temp_buf_destroy(&buf);
    return ret;
}
```

Use direct early `return` for simple leaf functions that do not need shared cleanup.

## 4) Includes and Linkage

Include order in `.c` files:
1. matching header (`module.h`)
2. system headers (`<stdlib.h>`, `<errno.h>`, ...)
3. project headers

All non-public helper functions in `.c` files should be `static`.

## 5) What This Standard Does Not Do

- It does **not** lock you into one architecture pattern.
- It does **not** enforce arbitrary limits like max parameters.
- It does **not** replace engineering judgment.

If code follows the mandatory rules and is readable, it is acceptable.

## 6) Enforcement

- Banned identifier checks are enforced by `scripts/check_banned.sh`.
- If a rule here conflicts with examples elsewhere, this file and `banned.md` take priority.
