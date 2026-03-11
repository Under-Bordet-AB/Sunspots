#!/usr/bin/env python3
"""Real-time backfill DB coverage monitor backed by the C SDK."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import signal
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


SLOT_SECONDS = 900
SDK_WINDOW_SLOTS = 672

SS_SDK_OK = 0
SS_SDK_CLAMPED = 1
SS_SDK_CLAMPED_PARTIAL_DATA = 2
SS_SDK_ERR_PARTIAL_DATA = 3

SS_SDK_VALUE_I64 = 0
SS_SDK_VALUE_F64 = 1
SS_SDK_VALUE_BOOL = 2

SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C = 0
SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT = 7
SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2 = 13
SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH = 20

VALID_READ_STATUSES = {
    SS_SDK_OK,
    SS_SDK_CLAMPED,
    SS_SDK_CLAMPED_PARTIAL_DATA,
    SS_SDK_ERR_PARTIAL_DATA,
}

RESET = "\x1b[0m"
BOLD = "\x1b[1m"
GREEN = "\x1b[32m"
RED = "\x1b[31m"
YELLOW = "\x1b[33m"
BLACK = "\x1b[30m"
DIM = "\x1b[2m"
REVERSE = "\x1b[7m"
BG_GREEN = "\x1b[42m"
BG_RED = "\x1b[41m"
BG_YELLOW = "\x1b[43m"

DATE_COL_MIN_WIDTH = 12
METRIC_COL_MIN_WIDTH = 10


class SdkValue(ctypes.Union):
    _fields_ = [
        ("i64", ctypes.c_int64),
        ("f64", ctypes.c_double),
        ("boolean", ctypes.c_bool),
    ]


class SdkSample(ctypes.Structure):
    _fields_ = [
        ("ts_utc", ctypes.c_int64),
        ("canonical", ctypes.c_int),
        ("value_type", ctypes.c_int),
        ("value", SdkValue),
        ("flags", ctypes.c_uint8),
    ]


class SdkSamplesOut(ctypes.Structure):
    _fields_ = [
        ("samples", ctypes.POINTER(SdkSample)),
        ("count", ctypes.c_size_t),
    ]


@dataclass(frozen=True)
class MetricSpec:
    label: str
    metric_id: int

    @property
    def key(self) -> str:
        return self.label


@dataclass
class ModuleSpec:
    name: str
    start_utc: int
    end_utc: int
    fillable_utc: int
    metrics: Sequence[MetricSpec]
    kind: str


@dataclass(frozen=True)
class ColumnSpec:
    module_name: str
    metric_label: str

    @property
    def key(self) -> str:
        return f"{self.module_name}:{self.metric_label}"

    @property
    def header(self) -> str:
        module_short = self.module_name.replace("Backfill", "")
        return f"{module_short} {self.metric_label}"


@dataclass
class DayMetricCoverage:
    present_slots: int
    expected_slots: int
    fillable_slots: int

    @property
    def complete(self) -> bool:
        return self.present_slots >= self.expected_slots

    @property
    def fillable_complete(self) -> bool:
        return self.present_slots >= self.fillable_slots

    @property
    def pending_slots(self) -> int:
        pending = self.expected_slots - self.fillable_slots
        return pending if pending > 0 else 0

    @property
    def missing_slots(self) -> int:
        missing = self.fillable_slots - self.present_slots
        return missing if missing > 0 else 0


class SdkClient:
    def __init__(self, shared_lib_path: Path):
        self._lib = ctypes.CDLL(str(shared_lib_path))
        self._lib.ss_sdk_db_get_canonical.argtypes = [
            ctypes.c_int64,
            ctypes.c_uint16,
            ctypes.c_int,
            ctypes.POINTER(SdkSamplesOut),
        ]
        self._lib.ss_sdk_db_get_canonical.restype = ctypes.c_int
        self._lib.ss_sdk_db_free_samples.argtypes = [ctypes.POINTER(SdkSamplesOut)]
        self._lib.ss_sdk_db_free_samples.restype = None

    def get_slots(self, metric_id: int, start_utc: int, quarters: int) -> Tuple[int, List[int]]:
        out = SdkSamplesOut()
        status = self._lib.ss_sdk_db_get_canonical(
            ctypes.c_int64(start_utc),
            ctypes.c_uint16(quarters),
            ctypes.c_int(metric_id),
            ctypes.byref(out),
        )
        slots: List[int] = []
        if status in VALID_READ_STATUSES and bool(out.samples):
            slots = [out.samples[i].ts_utc for i in range(out.count)]
        if bool(out.samples):
            self._lib.ss_sdk_db_free_samples(ctypes.byref(out))
        return status, slots


def align_to_slot(ts_utc: int) -> int:
    if ts_utc < 0:
        return 0
    return ts_utc - (ts_utc % SLOT_SECONDS)


def parse_utc_date(text: str) -> int:
    dt = datetime.strptime(text, "%Y-%m-%d").replace(tzinfo=timezone.utc)
    return int(dt.timestamp())


def iter_day_starts(start_utc: int, end_utc: int) -> Iterable[int]:
    current = start_utc
    while current < end_utc:
        yield current
        current += 86400


def discover_project_root(explicit_root: Optional[Path]) -> Path:
    if explicit_root is not None:
        return explicit_root.resolve()
    return Path(__file__).resolve().parents[1]


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def resolve_db_dir(project_root: Path, system_cfg: dict) -> Path:
    sdk_cfg = system_cfg.get("sdk", {})
    raw_db_dir = sdk_cfg.get("db_dir", "db")
    db_dir = Path(raw_db_dir)
    if not db_dir.is_absolute():
        db_dir = project_root / db_dir
    return db_dir.resolve()


def set_sdk_env(project_root: Path, root_cfg: dict) -> None:
    system_cfg = dict(root_cfg.get("system", {}))
    os.environ["SUNSPOTS_SYSTEM"] = json.dumps(system_cfg, separators=(",", ":"))
    os.environ["SS_SDK_DB_DIR"] = str(resolve_db_dir(project_root, system_cfg))


def resolve_sdk_shared(project_root: Path, explicit_path: Optional[Path]) -> Path:
    if explicit_path is not None:
        return explicit_path.resolve()
    env_path = os.environ.get("SUNSPOTS_SDK_SO")
    if env_path:
        return Path(env_path).resolve()
    candidates = [
        project_root / "build/debug/src/sdk/libsunspots_sdk_shared.so",
        project_root / "build/RelWithDebInfo/src/sdk/libsunspots_sdk_shared.so",
        project_root / "build/gprof/src/sdk/libsunspots_sdk_shared.so",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    raise FileNotFoundError(
        "Could not find libsunspots_sdk_shared.so. Build it with: "
        "cmake --build build/debug --target sunspots_sdk_shared"
    )


def module_metrics(module_name: str) -> Tuple[str, Sequence[MetricSpec]]:
    if module_name == "BackfillOpenMeteo":
        return (
            "weather",
            (
                MetricSpec("Temp", SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C),
                MetricSpec("Cloud", SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT),
                MetricSpec("Rad", SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2),
            ),
        )
    if module_name == "BackfillElprisjustnu":
        return ("price", (MetricSpec("Price", SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH),))
    raise ValueError(f"Unsupported backfill module for monitor: {module_name}")


def tomorrow_day_start_like_backfill() -> int:
    local_now = datetime.now().astimezone()
    local_tomorrow_midnight = (local_now + timedelta(days=1)).replace(hour=0, minute=0, second=0, microsecond=0)
    utc_date = local_tomorrow_midnight.astimezone(timezone.utc).strftime("%Y-%m-%d")
    return parse_utc_date(utc_date)


def build_modules(root_cfg: dict, selected_names: Optional[Sequence[str]]) -> List[ModuleSpec]:
    modules = []
    for module_cfg in root_cfg.get("modules", []):
        name = module_cfg.get("name")
        if not name or "backfill" not in module_cfg:
            continue
        if selected_names and name not in selected_names:
            continue
        backfill_cfg = module_cfg.get("backfill", {})
        if not backfill_cfg.get("enabled", False):
            continue
        kind, metrics = module_metrics(name)
        start_utc = parse_utc_date(backfill_cfg.get("start_date_utc", "2025-01-01"))
        if kind == "weather":
            lag_min = int(backfill_cfg.get("freshness_lag_minutes", 120))
            fillable_utc = align_to_slot(int(time.time()) - lag_min * 60)
            end_utc = fillable_utc
        else:
            fillable_utc = align_to_slot(int(time.time()))
            end_utc = tomorrow_day_start_like_backfill()
        if start_utc >= end_utc:
            continue
        modules.append(
            ModuleSpec(
                name=name,
                start_utc=start_utc,
                end_utc=end_utc,
                fillable_utc=fillable_utc,
                metrics=metrics,
                kind=kind,
            )
        )
    if selected_names:
        missing = [name for name in selected_names if name not in {m.name for m in modules}]
        if missing:
            raise ValueError(f"Requested backfill module(s) not found or not enabled: {', '.join(missing)}")
    if not modules:
        raise ValueError("No enabled backfill modules found in config.")
    return modules


def empty_day_grid(module: ModuleSpec) -> Dict[int, Dict[str, DayMetricCoverage]]:
    grid: Dict[int, Dict[str, DayMetricCoverage]] = {}
    for day_start in iter_day_starts(module.start_utc, module.end_utc):
        day_end = min(day_start + 86400, module.end_utc)
        expected = max(1, (day_end - day_start) // SLOT_SECONDS)
        fillable_end = min(day_end, max(day_start, module.fillable_utc))
        fillable = max(0, (fillable_end - day_start) // SLOT_SECONDS)
        grid[day_start] = {
            metric.label: DayMetricCoverage(
                present_slots=0,
                expected_slots=expected,
                fillable_slots=fillable,
            )
            for metric in module.metrics
        }
    return grid


def scan_metric(
    sdk: SdkClient,
    metric: MetricSpec,
    start_utc: int,
    end_utc: int,
) -> Tuple[Dict[int, int], List[str]]:
    counts: Dict[int, int] = {}
    errors: List[str] = []
    win_start = start_utc
    while win_start < end_utc:
        win_end = min(win_start + SDK_WINDOW_SLOTS * SLOT_SECONDS, end_utc)
        quarters = (win_end - win_start) // SLOT_SECONDS
        status, slots = sdk.get_slots(metric.metric_id, win_start, quarters)
        if status not in VALID_READ_STATUSES:
            errors.append(f"{metric.label}: sdk status {status} for window {win_start}-{win_end}")
            break
        for ts_utc in slots:
            if ts_utc < win_start or ts_utc >= win_end:
                continue
            day_start = ts_utc - (ts_utc % 86400)
            counts[day_start] = counts.get(day_start, 0) + 1
        win_start = win_end
    return counts, errors


def scan_module(sdk: SdkClient, module: ModuleSpec) -> Tuple[Dict[int, Dict[str, DayMetricCoverage]], List[str]]:
    grid = empty_day_grid(module)
    errors: List[str] = []
    for metric in module.metrics:
        counts, metric_errors = scan_metric(sdk, metric, module.start_utc, module.end_utc)
        errors.extend(metric_errors)
        for day_start, present in counts.items():
            if day_start in grid:
                current = grid[day_start][metric.label]
                grid[day_start][metric.label] = DayMetricCoverage(
                    present_slots=present,
                    expected_slots=current.expected_slots,
                    fillable_slots=current.fillable_slots,
                )
    return grid, errors


def color_cell(text: str, coverage: DayMetricCoverage, changed: bool) -> str:
    if changed:
        return f"{REVERSE}{BG_GREEN}{text}{RESET}"
    if coverage.complete:
        return f"{BG_GREEN}{text}{RESET}"
    if coverage.fillable_complete:
        return f"{BG_YELLOW}{BLACK}{text}{RESET}"
    return f"{BG_RED}{text}{RESET}"


def fit_text(text: str, width: int) -> str:
    if width <= 0:
        return ""
    if len(text) <= width:
        return text.ljust(width)
    if width == 1:
        return text[:1]
    return text[: width - 1] + "…"


def distribute_widths(total_width: int, count: int, minimum: int) -> List[int]:
    if count <= 0:
        return []
    widths = [minimum] * count
    extra = max(0, total_width - minimum * count)
    idx = 0
    while extra > 0:
        widths[idx] += 1
        extra -= 1
        idx = (idx + 1) % count
    return widths


def compute_table_widths(term_columns: int, metric_count: int) -> Tuple[int, List[int]]:
    spacer_count = metric_count
    minimum_total = DATE_COL_MIN_WIDTH + metric_count * METRIC_COL_MIN_WIDTH + spacer_count
    usable_columns = max(minimum_total, term_columns)
    content_width = usable_columns - spacer_count
    date_width = max(DATE_COL_MIN_WIDTH, min(16, content_width // 4))
    metric_total = max(metric_count * METRIC_COL_MIN_WIDTH, content_width - date_width)
    metric_widths = distribute_widths(metric_total, metric_count, METRIC_COL_MIN_WIDTH)
    used = date_width + sum(metric_widths)
    spare = content_width - used
    date_width += max(0, spare)
    return date_width, metric_widths


def all_days(modules: Sequence[ModuleSpec]) -> List[int]:
    days = set()
    for module in modules:
        for day_start in iter_day_starts(module.start_utc, module.end_utc):
            days.add(day_start)
    return sorted(days)


def summary_counts(module: ModuleSpec, grid: Dict[int, Dict[str, DayMetricCoverage]]) -> Tuple[int, int]:
    total = 0
    missing = 0
    for day_metrics in grid.values():
        for coverage in day_metrics.values():
            total += coverage.expected_slots
            missing += coverage.missing_slots
    return total, missing


def module_all_clear(grid: Dict[int, Dict[str, DayMetricCoverage]]) -> bool:
    return all(coverage.fillable_complete for day in grid.values() for coverage in day.values())


def pick_day_slice(
    ordered_days: Sequence[int],
    modules: Sequence[ModuleSpec],
    module_results: Dict[str, Dict[int, Dict[str, DayMetricCoverage]]],
    max_rows: int,
) -> Sequence[int]:
    if len(ordered_days) <= max_rows:
        return ordered_days
    first_incomplete = 0
    for idx, day_start in enumerate(ordered_days):
        incomplete = False
        for module in modules:
            day_metrics = module_results[module.name].get(day_start)
            if day_metrics and any(not cov.complete for cov in day_metrics.values()):
                incomplete = True
                break
        if incomplete:
            first_incomplete = idx
            break
    start_idx = max(0, first_incomplete - 2)
    end_idx = min(len(ordered_days), start_idx + max_rows)
    if end_idx - start_idx < max_rows:
        start_idx = max(0, end_idx - max_rows)
    return ordered_days[start_idx:end_idx]


def build_columns(modules: Sequence[ModuleSpec]) -> List[ColumnSpec]:
    columns: List[ColumnSpec] = []
    for module in modules:
        for metric in module.metrics:
            columns.append(ColumnSpec(module_name=module.name, metric_label=metric.label))
    return columns


def render(
    modules: Sequence[ModuleSpec],
    module_results: Dict[str, Dict[int, Dict[str, DayMetricCoverage]]],
    previous_results: Optional[Dict[str, Dict[int, Dict[str, DayMetricCoverage]]]],
    errors: Sequence[str],
    started_at: float,
) -> str:
    size = shutil.get_terminal_size((120, 40))
    term_columns = max(40, size.columns)
    lines: List[str] = []
    all_clear = all(module_all_clear(module_results[module.name]) for module in modules)
    banner = "ALL CLEAR" if all_clear else "HOLES REMAIN"
    banner_color = GREEN if all_clear else RED
    elapsed = int(time.time() - started_at)
    lines.append(f"{BOLD}{banner_color}{banner}{RESET}  elapsed={elapsed}s  updated={datetime.now().strftime('%H:%M:%S')}")
    if errors:
        lines.append(f"{YELLOW}errors:{RESET} {len(errors)}")
        for err in errors[:3]:
            lines.append(fit_text(f"  {err}", term_columns))
    legend = " ".join(
        [
            f"{BG_GREEN} complete {RESET}",
            f"{BG_YELLOW}{BLACK} pending {RESET}",
            f"{BG_RED} hole {RESET}",
            f"{REVERSE}{BG_GREEN} new {RESET}",
        ]
    )
    lines.append(f"legend: {legend}")
    lines.append("")
    for module in modules:
        grid = module_results[module.name]
        total, missing = summary_counts(module, grid)
        lines.append(
            f"{BOLD}{module.name}{RESET}  range={datetime.utcfromtimestamp(module.start_utc).strftime('%Y-%m-%d')}.."
            f"{datetime.utcfromtimestamp(module.end_utc).strftime('%Y-%m-%d')}  missing_slots={missing}/{total}"
        )
    lines.append("")
    ordered_days = all_days(modules)
    available_rows = max(6, size.lines - len(lines) - 4)
    day_slice = pick_day_slice(ordered_days, modules, module_results, min(available_rows, 18))
    columns = build_columns(modules)
    date_width, metric_widths = compute_table_widths(term_columns, len(columns))
    header = [fit_text("Date", date_width)]
    for column, width in zip(columns, metric_widths):
        header.append(fit_text(column.header, width))
    lines.append(" ".join(header))
    for day_start in day_slice:
        day_text = datetime.utcfromtimestamp(day_start).strftime("%Y-%m-%d")
        row = [fit_text(day_text, date_width)]
        for column, width in zip(columns, metric_widths):
            day_metrics = module_results[column.module_name].get(day_start)
            if day_metrics is None:
                row.append(f"{DIM}{' ' * width}{RESET}")
                continue
            coverage = day_metrics[column.metric_label]
            cell_text = f"{coverage.present_slots}/{coverage.expected_slots}".center(width)
            changed = False
            if previous_results is not None:
                prev_day_metrics = previous_results.get(column.module_name, {}).get(day_start)
                if prev_day_metrics is not None:
                    changed = coverage.present_slots > prev_day_metrics[column.metric_label].present_slots
            row.append(color_cell(cell_text, coverage, changed))
        lines.append(" ".join(row))
    if day_slice and len(day_slice) < len(ordered_days):
        hidden = len(ordered_days) - len(day_slice)
        lines.append(f"{DIM}... {hidden} day rows hidden; viewport tracks first incomplete day ...{RESET}")
    lines.append("")
    lines.append(f"{DIM}Ctrl-C to stop.{RESET}")
    return "\x1b[H\x1b[2J" + "\n".join(lines)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Watch backfill fill the SDK DB in real time.")
    parser.add_argument("--config", default="config/sunspots.json", help="Path to sunspots.json")
    parser.add_argument("--project-root", default=None, help="Project root; defaults to repo root")
    parser.add_argument("--sdk-so", default=None, help="Path to libsunspots_sdk_shared.so")
    parser.add_argument("--module", action="append", dest="modules", help="Backfill module name to monitor; repeatable")
    parser.add_argument("--interval", type=float, default=1.0, help="Refresh interval in seconds")
    parser.add_argument("--once", action="store_true", help="Render one snapshot and exit")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    project_root = discover_project_root(Path(args.project_root) if args.project_root else None)
    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = project_root / config_path
    root_cfg = load_json(config_path.resolve())
    set_sdk_env(project_root, root_cfg)
    sdk = SdkClient(resolve_sdk_shared(project_root, Path(args.sdk_so) if args.sdk_so else None))
    running = True

    def handle_signal(_signum: int, _frame: object) -> None:
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    started_at = time.time()
    previous_results: Optional[Dict[str, Dict[int, Dict[str, DayMetricCoverage]]]] = None
    while running:
        modules = build_modules(root_cfg, args.modules)
        module_results: Dict[str, Dict[int, Dict[str, DayMetricCoverage]]] = {}
        errors: List[str] = []
        for module in modules:
            grid, scan_errors = scan_module(sdk, module)
            module_results[module.name] = grid
            errors.extend(scan_errors)
        sys.stdout.write(render(modules, module_results, previous_results, errors, started_at))
        sys.stdout.flush()
        previous_results = module_results
        if args.once:
            break
        time.sleep(max(0.1, args.interval))
    return 0


if __name__ == "__main__":
    sys.exit(main())
