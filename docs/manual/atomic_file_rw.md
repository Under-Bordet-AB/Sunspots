# Atomic File RW Manual

**Library:** `src/libs/atomic_file_rw.h`  
**Status:** Stable compatibility wrapper (legacy API)  
**Recommendation:** New code should use the SDK APIs directly (`docs/manual/sdk.md`).

## 1. What This Is Now

`atomic_file_rw` is no longer the primary persistence layer.

It is a compatibility shim that keeps legacy call sites (`af_save` / `af_read`) working while routing data through the SDK database/logging stack.

- Keep using it if migration is too costly right now.
- Prefer SDK APIs for all new features and new modules.

## 2. Migration Guidance

- Legacy write path: `af_save(...)`
- Legacy read path: `af_read(...)`
- Target API for new work: `ss_sdk_db_write_record(...)`, `ss_sdk_db_get_canonical(...)`, `SS_LOG_*` macros
- SDK usage manual: `docs/manual/sdk.md`

Practical rule:
- Existing module with limited maintenance budget: staying on `atomic_file_rw` is acceptable.
- Any actively developed module: migrate to SDK APIs.

## 3. API (Still Supported)

```c
int af_save(const char *source, const char *type, const char *data);
char *af_read(size_t *out_size);
```

### `af_save`

Writes legacy payloads into SDK canonical storage.

- Returns `0` on success, `-1` on hard failure.
- `source`, `type`, `data` must be non-null (`EINVAL` + `-1` otherwise).
- `type` is currently accepted for compatibility but not used for routing.
- `data` is expected to be JSON.

Compatibility behavior:
- If JSON parse fails: payload is dropped, wrapper logs warning, return is still `0`.
- If `source` is unknown: payload is ignored, wrapper logs info, return is `0`.

### `af_read`

Returns newline-delimited JSON text (`malloc`-allocated; caller must `free`).

- Returns `NULL` on hard failure.
- Output is synthesized from SDK reads (not raw file bytes).
- Current shape is one latest sample per supported canonical metric, serialized as JSON lines.

## 4. Behavior Differences vs Old Manual

This wrapper no longer provides the old guarantees/mechanics that were documented earlier:

- No direct advisory-lock (`flock`) contract for users.
- No user-facing "shared JSONL file" as source of truth.
- No requirement to rely on `./database.jsonl` as operational storage.

`ATOMIC_FILE_DEFAULT_PATH` remains for source compatibility, but persistence is SDK-backed.

## 5. Source Routing (Current)

Current legacy source handling in `af_save`:

- `Openmeteo` / `openmeteo`: weather observations + forecast fields mapped into SDK canonical metrics.
- `Elprisjustnu` / `elprisjustnu`: electricity price fields mapped into SDK canonical metrics.
- Other sources: accepted for compatibility, ignored for data writes.

## 6. Recommended Direction

For the best developer experience and explicit semantics:

- Use SDK record APIs for writes.
- Use SDK canonical read APIs for reads.
- Use SDK log macros (`SS_LOG_DEBUG/INFO/WARN/ERROR`) for logging.

See `docs/manual/sdk.md` for usage-first examples.
