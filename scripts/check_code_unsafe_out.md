# Sunspots Unsafe API Usage Report

- Generated: `2026-02-13T21:45:15`
- Branch: `56-bug-fixes-sdk-and-atomic_file_rw`
- Commit: `f4991852ba96c7192db550f65893dc69a8fc54a9`

## Detailed Hits

### `core`

#### `core/main.c`
| Line | Function | Unsafe | Safe Alternative | Comment |
|---:|---|---|---|---|
| 440 | `daemon_module_timer_config` | `sscanf` | `strto*/manual parse` | easier error handling and range validation |
| 516 | `daemon_spawn_process` | `sprintf` | `snprintf` | bounded formatting |
| 517 | `daemon_spawn_process` | `sprintf` | `snprintf` | bounded formatting |
| 529 | `daemon_spawn_process` | `strerror` | `strerror_r` | thread-safe error-message conversion |
| 629 | `daemon_resolve_project_root` | `strerror` | `strerror_r` | thread-safe error-message conversion |

### `fetch`

#### `fetch/apis/fetch_elprisjustnu.c`
| Line | Function | Unsafe | Safe Alternative | Comment |
|---:|---|---|---|---|
| 52 | `main` | `localtime` | `localtime_r` | thread-safe time conversion |

### `frontend`

#### `frontend/endpoints.c`
| Line | Function | Unsafe | Safe Alternative | Comment |
|---:|---|---|---|---|
| 76 | `sanitize_path` | `strncpy` | `snprintf` | often leaves destination unterminated; prefer explicit size-aware copy |
