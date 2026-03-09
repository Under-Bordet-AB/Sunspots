# Core

## Purpose

The core module is the runtime supervisor.

- `src/core/daemon.c` is the daemon event loop.
- `src/core/module.c` is the process/module lifecycle manager.
- The daemon loads `sunspots.json`, spawns module binaries, watches them, and hot-reloads config.

## How it works

`daemon.c` does the OS-facing work:

- daemonization
- epoll setup
- signal handling through `signalfd`
- timer handling through `timerfd`
- config file watching through `inotify`

`module.c` does the policy work:

- parse `modules[]`
- resolve `bin_path`
- keep module state
- spawn child processes
- manage heartbeat vs timer modules
- preserve/restart modules across reloads

## Design strengths

- Good architectural split between daemon event loop and module policy.
- Process isolation is the right default for this system.
- `epoll` + `signalfd` + `timerfd` is a clean Linux-native design.

## Main weaknesses

- `module.c` is doing too much in one file: config parsing, state reconciliation, spawning, restart logic, and timer setup.
- The config schema still leaks stringly-typed keys like `Timer-type`, `Rel-time`, `Abs-time`.
- Hot-reload behavior is subtle and hard to test in isolation because reconciliation logic is not strongly factored.

## What to watch out for

- Any change to config shape tends to ripple through `module_load`.
- Daemon behavior is very stateful; regressions often show up only on reload or restart paths.
- Heartbeat and timer behavior share storage but have different semantics, so mixed logic here is easy to get wrong.
