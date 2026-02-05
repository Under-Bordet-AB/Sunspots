# Sunspots

Sunspots is a file-contract energy pipeline in C for weather/energy planning:

`fetch -> normalize -> NDJSON DB -> compute -> endpoint JSON -> HTTP serve`

## Current Runtime

- `sunspotsd`: supervisor (signal-driven worker monitoring)
- `fetch_openmeteo`: Open-Meteo fetcher
- `fetch_smhi`: SMHI fetcher
- `calc_smhi_avg_temp`: SMHI prognosis average temperature calculator
- `sunspots_server`: epoll + inotify endpoint server with cache

## Build

```bash
make all
```

## Test

```bash
make test
```

## Run

```bash
make run
```

Daemon auto-loads: `config/sunspots.json`

## API Test

```bash
curl -s http://127.0.0.1:8080/api/smhi_avg_temperature | jq
```

## Documentation

- Active design: `docs/cfg_load_plan.md`
- Developer manual: `docs/developer_manual.md`
- Future roadmap: `docs/future_plan.md`
