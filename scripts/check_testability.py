#!/usr/bin/env python3
"""Lightweight static testability checker for C/C++ source files.

This is heuristic by design. It does not prove testability, but it helps
identify files that are harder to unit test and why.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Iterable, List


MAIN_RE = re.compile(r"\bint\s+main\s*\(")
NO_MAIN_GUARD_RE = re.compile(r"#ifndef\s+[A-Z0-9_]*NO_MAIN\b")
INFINITE_LOOP_RE = re.compile(r"\bwhile\s*\(\s*1\s*\)|\bfor\s*\(\s*;\s*;\s*\)")
BLOCKING_SLEEP_RE = re.compile(r"\bnanosleep\s*\(|\bsleep\s*\(")

# Calls that commonly indicate strong runtime coupling and harder unit tests.
HARD_COUPLING_RE = re.compile(
    r"\b("
    r"fork|execv|execvp|execve|kill|waitpid|wait4|"
    r"socket|accept|bind|listen|connect|recv|send|"
    r"epoll_create1|epoll_ctl|epoll_wait|"
    r"inotify_init|inotify_init1|inotify_add_watch|"
    r"timerfd_create|timerfd_settime|"
    r"pthread_create|pthread_join|sigaction|signal|"
    r"openlog|syslog|closelog"
    r")\s*\("
)


COMMENT_OR_STRING_RE = re.compile(
    r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
    re.DOTALL,
)


def display_path(path: pathlib.Path) -> str:
    """Render paths without machine-specific absolute prefixes."""
    try:
        return os.path.relpath(path, pathlib.Path.cwd())
    except Exception:
        return path.name


@dataclass
class FileReport:
    path: pathlib.Path
    score: int
    findings: List[str]


def score_file(path: pathlib.Path, text: str) -> FileReport:
    score = 100
    findings: List[str] = []

    cleaned = COMMENT_OR_STRING_RE.sub(" ", text)

    has_main = bool(MAIN_RE.search(cleaned))
    has_no_main_guard = bool(NO_MAIN_GUARD_RE.search(cleaned))
    has_infinite_loop = bool(INFINITE_LOOP_RE.search(cleaned))
    has_blocking_sleep = bool(BLOCKING_SLEEP_RE.search(cleaned))
    hard_calls = HARD_COUPLING_RE.findall(cleaned)

    if has_main and not has_no_main_guard:
        score -= 35
        findings.append("has `main()` without `NO_MAIN` compile guard")
    elif has_main and has_no_main_guard:
        score -= 10
        findings.append("has `main()` but compile guard exists")

    if has_infinite_loop:
        score -= 25
        findings.append("contains explicit infinite loop")

    if has_blocking_sleep:
        score -= 10
        findings.append("contains blocking sleep calls")

    if hard_calls:
        # Unique calls for better signal quality.
        unique_calls = sorted(set(hard_calls))
        penalty = min(30, len(unique_calls) * 4)
        score -= penalty
        findings.append(
            "runtime coupling calls: " + ", ".join(unique_calls[:8]) +
            ("..." if len(unique_calls) > 8 else "")
        )

    score = max(0, score)
    return FileReport(path=path, score=score, findings=findings)


def iter_source_files(root: pathlib.Path) -> Iterable[pathlib.Path]:
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        if p.suffix.lower() not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
            continue
        yield p


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Heuristic testability checker for source files."
    )
    parser.add_argument(
        "--root",
        default=None,
        help="Directory to scan. Default: repo-root/src (resolved from script path).",
    )
    parser.add_argument(
        "--max",
        type=int,
        default=30,
        help="Maximum rows to print (worst-first).",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="Output report path. Default: repo-root/scripts/out/check_testability_out.md",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Also print the generated report to stdout.",
    )
    return parser.parse_args(argv)


def render_report(root: pathlib.Path, reports: List[FileReport], max_rows: int) -> str:
    flagged = [r for r in reports if r.findings]
    total = len(reports)

    lines: List[str] = []
    lines.append("# Hard to Test Files Report")
    lines.append("")
    lines.append(f"- Generated: `{dt.datetime.now().isoformat(timespec='seconds')}`")
    lines.append(f"- Total files: `{total}`")
    lines.append(f"- Flagged hard-to-test files: `{len(flagged)}`")
    lines.append("")
    lines.append("## Notes")
    lines.append("")
    lines.append("- This report intentionally lists deterministic findings only.")
    lines.append("- For natural-language explanation, paste this report into an LLM.")
    lines.append("")
    lines.append("Suggested prompt:")
    lines.append("")
    lines.append("```text")
    lines.append("Explain why each finding makes the file hard to unit test.")
    lines.append("For each file, propose the smallest refactor that improves testability")
    lines.append("without changing behavior.")
    lines.append("```")
    lines.append("")
    lines.append("## Hard to Test Files")
    lines.append("")
    if not flagged:
        lines.append("No hard-to-test files detected by current heuristics.")
    else:
        for rep in flagged[: max(0, max_rows)]:
            lines.append(f"### `{rep.path}`")
            lines.append("")
            lines.append("Findings:")
            for finding in rep.findings:
                lines.append(f"- {finding}")
            lines.append("")
            lines.append("LLM Notes:")
            lines.append("")
            lines.append("Explanation:")
            lines.append("")
            lines.append("Suggested minimal refactor:")
            lines.append("")

    lines.append("")
    return "\n".join(lines)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    root = pathlib.Path(args.root) if args.root is not None else (repo_root / "src")
    out_path = pathlib.Path(args.out) if args.out is not None else (repo_root / "scripts" / "out" / "check_testability_out.md")
    if not root.exists():
        print(f"error: root path does not exist: {display_path(root)}", file=sys.stderr)
        return 2

    reports: List[FileReport] = []
    for path in iter_source_files(root):
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError as exc:
            print(f"warn: failed to read {display_path(path)}: {exc}", file=sys.stderr)
            continue
        reports.append(score_file(path.relative_to(repo_root), text))

    reports.sort(key=lambda r: (r.score, str(r.path)))
    report_text = render_report(root, reports, args.max)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(report_text, encoding="utf-8")
    print(f"Wrote testability report: {display_path(out_path)}")
    if args.stdout:
        print()
        print(report_text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
