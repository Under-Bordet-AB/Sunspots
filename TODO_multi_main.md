# TODO: Multi-Process Architecture & Build System

## Current State
The project currently contains multiple files with `main` functions in `src/core/`:
- `daemon.c`: The supervisor process.
- `sunspots_core.c`: A worker process (currently test code).
- `fetch_data.c`: A worker process (currently test code).

The `makefile` manually filters these out using the `CONFLICT_APPS` variable to prevent linker collisions when building the primary `sunspots.out` binary.

## Architecture Concept
We are moving towards a **Multi-Process "Flat" Model**:
1. **Supervisor (Daemon)**: Responsible for spawning, monitoring, and restarting workers.
2. **Worker Nodes**: Specialized processes performing discrete tasks (fetching, computing).
3. **IPC**: Heartbeats are currently implemented using Linux Real-Time signals (`SIGRTMIN`).

## TODO List

### 1. Build System Updates
- [ ] Update `makefile` to build each component in `CONFLICT_APPS` as a standalone executable.
- [ ] Ensure `make all` builds the daemon and all worker binaries.
- [ ] Place worker binaries in a standard location (e.g., `build/bin/workers/`) so the daemon can find them.

### 2. Configuration Integration
- [ ] The `daemon` should load the list of processes to spawn from `config/sunspots.json`.
- [ ] Define a standard JSON schema for worker definitions (path, heartbeat interval, arguments).

### 3. Production Readiness
- [ ] Remove "THIS IS TEST CODE!" headers from `core` modules.
- [ ] Implement robust logging (`jj_log`) in the daemon and workers (workers may need to log to files or a socket to avoid interleaved `stdout`).
- [ ] Replace `atoi` and `printf` in `core/` modules with standard-compliant alternatives.

### 4. IPC & Health Monitoring
- [ ] Standardize the heartbeat signal protocol.
- [ ] Implement "Graceful Shutdown" propagation (Daemon -> Workers).
- [ ] Consider using UNIX domain sockets if workers need to pass complex data back to the supervisor.
