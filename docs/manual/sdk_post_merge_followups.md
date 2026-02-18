# SDK Post-Merge Followups

This document tracks follow-up work that should be split into focused PRs after the current SDK merge.

## Goal

Keep this branch SDK-focused, then finish runtime wiring and cleanup in small, reviewable issues.

## Recommended Issue Breakdown

1. Daemon wiring: map `common.sdk.*` to SDK env vars
- Scope: `src/core/main.c` (or daemon config layer)
- Map to:
  - `SS_SDK_DB_PATH`
  - `SS_SDK_DEBUG`
  - `SS_SDK_LOG_MIRROR_ENABLED`
  - `SS_SDK_LOG_MIRROR_PATH`

2. Config schema/docs: formalize `common.sdk`
- Scope: config docs + sample config
- Keys:
  - `db_path`
  - `debug_mode`
  - `log_mirror_enabled`
  - `log_mirror_path`