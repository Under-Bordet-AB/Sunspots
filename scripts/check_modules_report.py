#!/usr/bin/env python3
"""Generate a module-level engineering report for Sunspots."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Tuple


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".def"}


def display_path(path: pathlib.Path) -> str:
    """Render paths without machine-specific absolute prefixes."""
    try:
        return os.path.relpath(path, pathlib.Path.cwd())
    except Exception:
        return path.name


@dataclass
class ModuleCoverage:
    line_cov: int = 0
    line_tot: int = 0
    fn_cov: int = 0
    fn_tot: int = 0
    br_cov: int = 0
    br_tot: int = 0


@dataclass
class ModuleReport:
    name: str
    files: List[str] = field(default_factory=list)
    test_count: int = 0
    coverage: ModuleCoverage = field(default_factory=ModuleCoverage)
    hard_files: Dict[str, List[str]] = field(default_factory=dict)


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate module report.")
    parser.add_argument(
        "--src-root",
        default=None,
        help="Source root. Default: repo-root/src",
    )
    parser.add_argument(
        "--tests-cmake",
        default=None,
        help="Path to tests/CMakeLists.txt. Default: repo-root/tests/CMakeLists.txt",
    )
    parser.add_argument(
        "--coverage-json",
        default=None,
        help=(
            "Path to gcovr JSON report. "
            "Default: repo-root/scripts/out/coverage/coverage_src_current.json"
        ),
    )
    parser.add_argument(
        "--testability-report",
        default=None,
        help=(
            "Path to hard-to-test report. "
            "Default: repo-root/scripts/check_testability_out.md"
        ),
    )
    parser.add_argument(
        "--out",
        default=None,
        help="Output report path. Default: repo-root/scripts/out/check_modules_out.md",
    )
    return parser.parse_args(argv)


def discover_modules(src_root: pathlib.Path) -> Dict[str, ModuleReport]:
    modules: Dict[str, ModuleReport] = {}
    for path in sorted(src_root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        rel = path.relative_to(src_root).as_posix()
        parts = rel.split("/")
        if not parts:
            continue
        module = parts[0]
        modules.setdefault(module, ModuleReport(name=module)).files.append(rel)
    return modules


def parse_test_counts(tests_cmake: pathlib.Path) -> Dict[str, int]:
    counts: Dict[str, int] = defaultdict(int)
    if not tests_cmake.exists():
        return counts

    pattern = re.compile(r"component:([A-Za-z0-9_]+)")
    for line in tests_cmake.read_text(encoding="utf-8", errors="ignore").splitlines():
        m = pattern.search(line)
        if m:
            counts[m.group(1)] += 1
    return counts


def _coverage_from_file_entry(entry: dict) -> Tuple[int, int, int, int, int, int]:
    lines = entry.get("lines", [])
    line_tot = len(lines)
    line_cov = sum(1 for ln in lines if ln.get("count", 0) > 0)

    functions = entry.get("functions", [])
    fn_tot = len(functions)
    fn_cov = sum(1 for fn in functions if fn.get("execution_count", 0) > 0)

    br_tot = 0
    br_cov = 0
    for ln in lines:
        for br in (ln.get("branches", []) or []):
            br_tot += 1
            if br.get("count", 0) > 0:
                br_cov += 1

    return line_cov, line_tot, fn_cov, fn_tot, br_cov, br_tot


def parse_coverage(coverage_json: pathlib.Path) -> Dict[str, ModuleCoverage]:
    out: Dict[str, ModuleCoverage] = defaultdict(ModuleCoverage)
    if not coverage_json.exists():
        return out

    data = json.loads(coverage_json.read_text(encoding="utf-8"))
    for entry in data.get("files", []):
        path = entry.get("file", "")
        if not path.startswith("src/"):
            continue
        parts = path.split("/")
        if len(parts) < 3:
            continue
        module = parts[1]
        lc, lt, fc, ft, bc, bt = _coverage_from_file_entry(entry)
        cov = out[module]
        cov.line_cov += lc
        cov.line_tot += lt
        cov.fn_cov += fc
        cov.fn_tot += ft
        cov.br_cov += bc
        cov.br_tot += bt
    return out


def parse_hard_to_test(report_path: pathlib.Path) -> Dict[str, Dict[str, List[str]]]:
    out: Dict[str, Dict[str, List[str]]] = defaultdict(lambda: defaultdict(list))
    if not report_path.exists():
        return out

    in_hard_section = False
    current_file = None
    in_findings = False

    for raw_line in report_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw_line.strip()
        if line == "## Hard to Test Files":
            in_hard_section = True
            continue
        if not in_hard_section:
            continue

        m = re.match(r"### `(.+)`", line)
        if m:
            current_file = m.group(1)
            in_findings = False
            continue

        if line == "Findings:":
            in_findings = True
            continue
        if line.startswith("LLM Notes:"):
            in_findings = False
            continue

        if in_findings and current_file and line.startswith("- "):
            path = pathlib.Path(current_file).as_posix()
            parts = path.split("/")
            module = "unknown"
            if "src" in parts:
                idx = parts.index("src")
                if idx + 1 < len(parts):
                    module = parts[idx + 1]
            out[module][current_file].append(line[2:])

    return out


def pct(cov: int, tot: int) -> str:
    if tot <= 0:
        return "n/a"
    return f"{(100.0 * cov / tot):.1f}%"


def build_strengths(mod: ModuleReport) -> List[str]:
    strengths: List[str] = []
    if mod.test_count > 0:
        strengths.append(f"Has {mod.test_count} unit test target(s) in `tests/CMakeLists.txt`.")

    if mod.coverage.line_tot > 0 and (100.0 * mod.coverage.line_cov / mod.coverage.line_tot) >= 70.0:
        strengths.append(f"Line coverage is {pct(mod.coverage.line_cov, mod.coverage.line_tot)}.")
    if mod.coverage.fn_tot > 0 and (100.0 * mod.coverage.fn_cov / mod.coverage.fn_tot) >= 90.0:
        strengths.append(f"Function coverage is {pct(mod.coverage.fn_cov, mod.coverage.fn_tot)}.")

    if not mod.hard_files:
        strengths.append("No hard-to-test files were flagged by current heuristics.")

    if not strengths:
        strengths.append("No clear strengths detected by current automated signals.")
    return strengths


def build_concerns(mod: ModuleReport) -> List[str]:
    concerns: List[str] = []
    if mod.test_count == 0:
        concerns.append("No dedicated unit test target is currently labeled for this module.")

    if mod.coverage.line_tot > 0 and (100.0 * mod.coverage.line_cov / mod.coverage.line_tot) < 70.0:
        concerns.append(f"Line coverage is low: {pct(mod.coverage.line_cov, mod.coverage.line_tot)}.")

    if mod.hard_files:
        concerns.append(f"{len(mod.hard_files)} file(s) flagged as hard to test.")

    if not concerns:
        concerns.append("No immediate concerns detected by current automated signals.")
    return concerns


def render_report(
    modules: Dict[str, ModuleReport],
    src_root: pathlib.Path,
    coverage_json: pathlib.Path,
    testability_report: pathlib.Path,
) -> str:
    _ = src_root
    lines: List[str] = []
    lines.append("# Sunspots Modules Report")
    lines.append("")
    lines.append(f"- Generated: `{dt.datetime.now().isoformat(timespec='seconds')}`")
    lines.append(f"- Coverage input: `{coverage_json.name}`")
    lines.append(f"- Testability input: `{testability_report.name}`")
    lines.append(f"- Modules covered: `{len(modules)}`")
    lines.append("")
    lines.append("## Index")
    lines.append("")
    for name in sorted(modules):
        lines.append(f"- [`{name}`](#module-{name}) | [`src/{name}`](../src/{name})")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append("| Module | Path | Source files | Unit tests | Line coverage | Function coverage | Branch coverage | Hard-to-test files |")
    lines.append("|---|---|---:|---:|---|---|---|---:|")

    for name in sorted(modules):
        mod = modules[name]
        lines.append(
            f"| [`{name}`](#module-{name}) | [`src/{name}`](../src/{name}) | {len(mod.files)} | {mod.test_count} | "
            f"{pct(mod.coverage.line_cov, mod.coverage.line_tot)} | "
            f"{pct(mod.coverage.fn_cov, mod.coverage.fn_tot)} | "
            f"{pct(mod.coverage.br_cov, mod.coverage.br_tot)} | "
            f"{len(mod.hard_files)} |"
        )

    lines.append("")
    lines.append("## Module Details")
    lines.append("")

    for name in sorted(modules):
        mod = modules[name]
        lines.append(f"<a id=\"module-{name}\"></a>")
        lines.append(f"### `{name}`")
        lines.append("")
        lines.append("Good things:")
        for item in build_strengths(mod):
            lines.append(f"- {item}")
        lines.append("")

        lines.append("Concerns:")
        for item in build_concerns(mod):
            lines.append(f"- {item}")
        lines.append("")

        if mod.hard_files:
            lines.append("Hard-to-test findings:")
            for file_path in sorted(mod.hard_files):
                lines.append(f"- `{file_path}`")
                for finding in mod.hard_files[file_path]:
                    lines.append(f"  - {finding}")
            lines.append("")

    return "\n".join(lines) + "\n"


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    src_root = pathlib.Path(args.src_root) if args.src_root else (repo_root / "src")
    tests_cmake = pathlib.Path(args.tests_cmake) if args.tests_cmake else (repo_root / "tests" / "CMakeLists.txt")
    coverage_json = (
        pathlib.Path(args.coverage_json)
        if args.coverage_json
        else (repo_root / "scripts" / "out" / "coverage" / "coverage_src_current.json")
    )
    testability_report = pathlib.Path(args.testability_report) if args.testability_report else (repo_root / "scripts" / "out" / "check_testability_out.md")
    out_path = pathlib.Path(args.out) if args.out else (repo_root / "scripts" / "out" / "check_modules_out.md")

    if not src_root.exists():
        print(f"error: source root not found: {src_root}", file=sys.stderr)
        return 2

    modules = discover_modules(src_root)
    test_counts = parse_test_counts(tests_cmake)
    cov_map = parse_coverage(coverage_json)
    hard_map = parse_hard_to_test(testability_report)

    for name, mod in modules.items():
        mod.test_count = test_counts.get(name, 0)
        mod.coverage = cov_map.get(name, ModuleCoverage())
        mod.hard_files = dict(hard_map.get(name, {}))

    out_text = render_report(modules, src_root, coverage_json, testability_report)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(out_text, encoding="utf-8")
    print(f"Wrote modules report: {display_path(out_path)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
