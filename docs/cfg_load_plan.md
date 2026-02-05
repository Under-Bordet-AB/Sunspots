## Config Loading Architecture Plan (Daemon Root + Worker Subtrees)

### Summary
Use a **single root config** as daemon-owned source of truth, then generate **per-worker subtree config files** and pass each path via env at process start.  
This preserves decoupling, supports versioned reloads, and stays aligned with your file-based communication model.

### Architecture
- Daemon startup flow:
  1. Load root config from file.
  2. Apply overrides in order: **args > env > file**.
  3. Validate root + per-worker required fields.
  4. Materialize worker slice files under `runtime/config/<worker>.json`.
  5. Spawn workers with `SUNSPOTS_CONFIG_PATH` set to their slice.
- Worker flow:
  1. Read only `SUNSPOTS_CONFIG_PATH`.
  2. Load slice config (no root parsing).
  3. Run with defaults where allowed.
  4. Continue heartbeat to daemon.
- Reload flow:
  - Daemon watches root config for changes.
  - On change: rebuild/validate slices, bump config version, restart only affected workers.
  - If a worker slice is invalid at reload: restart with defaults (per selected policy).

### Config Shape and Slicing
- Root JSON keeps full system structure (daemon-only).
- Worker slice shape:
  - `common`: shared runtime fields (paths, schema version, heartbeat interval, log level).
  - `worker`: worker-specific subtree (`modules.<worker_name>`).
- Workers must treat any keys outside `common` + `worker` as unsupported.

### Public Interfaces / Contract Changes
- New daemon-to-worker env contract:
  - `SUNSPOTS_CONFIG_PATH` (required)
  - `SUNSPOTS_CONFIG_VERSION` (required, monotonic string/int)
- New runtime artifact contract:
  - `runtime/config/<worker>.json` (daemon-written, worker-read)
- Config module usage pattern:
  - Daemon: load root + slice
  - Workers: load slice only
- Validation contract:
  - Fail-fast at daemon startup if required global structure is invalid.

### Failure and Recovery Rules
- Startup:
  - Invalid root config => daemon does not start workers.
- Reload:
  - Slice generation/validation fails for a worker => worker restart still proceeds with defaults.
  - Unaffected workers continue running.
- Always log config version, worker name, and validation result on startup/restart.

### Test Cases and Scenarios
- Unit:
  - Root load precedence (`file`, then `env`, then `args` override).
  - Slice extraction correctness for each worker.
  - Required-key validation behavior.
- Integration:
  - Daemon startup creates slice files and workers read correct paths.
  - Root config change restarts only affected workers.
  - Worker receives new `SUNSPOTS_CONFIG_VERSION` after reload.
  - Invalid reload config path: worker uses defaults and still starts.
- Regression:
  - Workers cannot access unrelated module config keys.
  - Existing heartbeat supervision behavior remains unchanged.

### Implementation Steps
1. Define root schema sections: `common` + `modules.<worker>`.
2. Add daemon slice-builder utility (extract + write JSON atomically).
3. Add spawn env wiring for `SUNSPOTS_CONFIG_PATH` and version.
4. Update workers to load config only from passed slice path.
5. Add reload watcher + affected-worker restart logic.
6. Add tests (unit + integration) and logging for versioned config lifecycle.

### Assumptions and Defaults
- Assumed worker set is known at daemon startup.
- Assumed slice files are local filesystem artifacts under daemon control.
- Defaulted to **worker-only subtree slices** for isolation.
- Defaulted to **versioned restart reload** (not hot-reload in-process).
- Defaulted to **daemon-owned root config only**; workers never parse full root.
