#!/usr/bin/env python3
"""Generate lightweight code-spotlight reports for the repository.

Outputs:
- Main spotlight report with function size/risk hotspots.
- Unsafe API usage report driven by a rules file (Markdown table or TSV).
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

SOURCE_EXTENSIONS = {".c", ".h", ".cpp", ".hpp"}
CONTROL_KEYWORDS = {"if", "for", "while", "switch", "else", "do"}
ALLOC_FUNCTIONS = ("malloc", "calloc", "realloc", "strdup", "strndup")
EXCLUDED_SOURCE_RELATIVE_PATHS = {
    "libs/json/cJSON.c",
    "libs/json/cJSON.h",
}

IGNORED_CONST_TYPE_KEYWORDS = {
    "cJSON",
}


@dataclass
class FunctionInfo:
    module: str
    file_path: str
    name: str
    start_line: int
    end_line: int
    is_static: bool = False
    parameter_count: int = 0

    decision_count: int = 0
    complexity_score: int = 1
    max_nesting_depth: int = 0
    max_loop_depth: int = 0
    allocation_calls: int = 0
    return_count: int = 0
    risk_score: int = 0
    signature_text: str = ""
    parameters: List["ParameterInfo"] = field(default_factory=list)

    @property
    def length(self) -> int:
        return self.end_line - self.start_line + 1


@dataclass
class ParameterInfo:
    raw: str
    name: str
    is_pointer: bool
    has_const: bool


@dataclass
class UnsafeRule:
    unsafe: str
    safe: str
    comment: str
    pattern: re.Pattern[str]


@dataclass
class UnsafeHit:
    module: str
    file_path: str
    line_no: int
    unsafe: str
    safe: str
    comment: str
    line_text: str
    function_name: str


@dataclass
class StaticCandidate:
    module: str
    file_path: str
    function_name: str
    line_no: int
    reason: str


@dataclass
class ConstCandidate:
    module: str
    file_path: str
    function_name: str
    line_no: int
    parameter_name: str
    parameter_raw: str


def discover_source_files(src_root: Path) -> List[Path]:
    paths: List[Path] = []
    for path in src_root.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_EXTENSIONS:
            continue
        rel_path = str(path.relative_to(src_root))
        if rel_path in EXCLUDED_SOURCE_RELATIVE_PATHS:
            continue
        if path.is_file() and path.suffix in SOURCE_EXTENSIONS:
            paths.append(path)
    return sorted(paths)


def module_from_path(path: Path, src_root: Path) -> str:
    rel = path.relative_to(src_root)
    return rel.parts[0] if rel.parts else "unknown"


def strip_c_line(line: str, in_block_comment: bool) -> Tuple[str, bool]:
    """Remove strings/chars/comments enough for heuristic scanning."""
    out: List[str] = []
    i = 0
    n = len(line)
    in_string = False
    in_char = False

    while i < n:
        ch = line[i]
        nxt = line[i + 1] if i + 1 < n else ""

        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 2
            else:
                i += 1
            continue

        if in_string:
            if ch == "\\" and i + 1 < n:
                i += 2
                continue
            if ch == '"':
                in_string = False
            i += 1
            continue

        if in_char:
            if ch == "\\" and i + 1 < n:
                i += 2
                continue
            if ch == "'":
                in_char = False
            i += 1
            continue

        if ch == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue
        if ch == "/" and nxt == "/":
            break
        if ch == '"':
            in_string = True
            i += 1
            continue
        if ch == "'":
            in_char = True
            i += 1
            continue

        out.append(ch)
        i += 1

    return "".join(out), in_block_comment


def extract_function_name(signature_text: str) -> Optional[str]:
    compact = re.sub(r"\s+", " ", signature_text).strip()
    if not compact:
        return None

    if compact.startswith(("typedef ", "struct ", "enum ", "union ")):
        return None

    paren = compact.find("(")
    if paren < 0:
        return None

    token_space = compact[:paren].strip()
    if not token_space:
        return None

    name_match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", token_space)
    if not name_match:
        return None

    name = name_match.group(1)
    if name in CONTROL_KEYWORDS:
        return None

    prefix = token_space[: name_match.start()].strip()
    if not prefix:
        return None

    return name


def extract_param_text(signature_text: str) -> str:
    compact = re.sub(r"\s+", " ", signature_text).strip()
    start = compact.find("(")
    if start < 0:
        return ""

    depth = 0
    end = -1
    for i in range(start, len(compact)):
        ch = compact[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                end = i
                break

    if end < 0:
        return ""

    return compact[start + 1 : end].strip()


def estimate_parameter_count(signature_text: str) -> int:
    params = extract_param_text(signature_text)
    if not params or params == "void":
        return 0
    return params.count(",") + 1


def split_parameter_list(params_text: str) -> List[str]:
    parts: List[str] = []
    cur: List[str] = []
    paren_depth = 0

    for ch in params_text:
        if ch == "(":
            paren_depth += 1
        elif ch == ")":
            paren_depth = max(0, paren_depth - 1)

        if ch == "," and paren_depth == 0:
            part = "".join(cur).strip()
            if part:
                parts.append(part)
            cur = []
            continue

        cur.append(ch)

    tail = "".join(cur).strip()
    if tail:
        parts.append(tail)
    return parts


def parse_parameters(signature_text: str) -> List[ParameterInfo]:
    params_text = extract_param_text(signature_text)
    if not params_text or params_text == "void":
        return []

    parsed: List[ParameterInfo] = []
    for raw_param in split_parameter_list(params_text):
        p = raw_param.strip()
        if not p or p == "...":
            continue

        # Function pointer parameters are noisy for this heuristic; skip.
        if "(*" in p:
            continue

        name_match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*$", p)
        if not name_match:
            continue

        name = name_match.group(1)
        before_name = p[: name_match.start()]
        is_pointer = "*" in before_name
        has_const = re.search(r"\bconst\b", p) is not None

        parsed.append(
            ParameterInfo(
                raw=p,
                name=name,
                is_pointer=is_pointer,
                has_const=has_const,
            )
        )

    return parsed


def is_static_function(signature_text: str) -> bool:
    compact = re.sub(r"\s+", " ", signature_text).strip()
    return compact.startswith("static ")


def find_functions_in_file(path: Path, module: str, src_root: Path) -> List[FunctionInfo]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    rel_path = str(path.relative_to(src_root))

    functions: List[FunctionInfo] = []

    in_block_comment = False
    in_function = False
    function_depth = 0
    current_name = ""
    current_start = 0
    current_is_static = False
    current_param_count = 0
    current_signature_text = ""
    current_parameters: List[ParameterInfo] = []

    signature_lines: List[Tuple[int, str]] = []

    for i, raw in enumerate(lines, start=1):
        cleaned, in_block_comment = strip_c_line(raw, in_block_comment)

        if not in_function:
            stripped = cleaned.strip()

            if stripped.startswith("#"):
                continue

            if stripped:
                signature_lines.append((i, cleaned))
                if len(signature_lines) > 40:
                    signature_lines = signature_lines[-40:]

            if ";" in cleaned and "{" not in cleaned:
                signature_lines = []
                continue

            if "{" in cleaned:
                brace_index = cleaned.find("{")
                if signature_lines:
                    text_parts: List[str] = []
                    start_line = signature_lines[0][0]
                    for ln, txt in signature_lines:
                        if ln == i:
                            text_parts.append(txt[:brace_index])
                        else:
                            text_parts.append(txt)
                    signature_text = "\n".join(text_parts)
                else:
                    start_line = i
                    signature_text = cleaned[:brace_index]

                function_name = extract_function_name(signature_text)
                if function_name:
                    compact_signature = re.sub(r"\s+", " ", signature_text).strip()
                    parsed_parameters = parse_parameters(signature_text)
                    in_function = True
                    current_name = function_name
                    current_start = start_line
                    current_is_static = is_static_function(signature_text)
                    current_param_count = estimate_parameter_count(signature_text)
                    current_signature_text = compact_signature
                    current_parameters = parsed_parameters

                    opens = cleaned.count("{")
                    closes = cleaned.count("}")
                    function_depth = opens - closes

                    if function_depth <= 0:
                        functions.append(
                            FunctionInfo(
                                module=module,
                                file_path=rel_path,
                                name=current_name,
                                start_line=current_start,
                                end_line=i,
                                is_static=current_is_static,
                                parameter_count=current_param_count,
                                signature_text=current_signature_text,
                                parameters=current_parameters,
                            )
                        )
                        in_function = False
                        function_depth = 0

                signature_lines = []
        else:
            function_depth += cleaned.count("{")
            function_depth -= cleaned.count("}")

            if function_depth <= 0:
                functions.append(
                    FunctionInfo(
                        module=module,
                        file_path=rel_path,
                        name=current_name,
                        start_line=current_start,
                        end_line=i,
                        is_static=current_is_static,
                        parameter_count=current_param_count,
                        signature_text=current_signature_text,
                        parameters=current_parameters,
                    )
                )
                in_function = False
                function_depth = 0
                signature_lines = []

    return functions


def decision_count(cleaned_line: str) -> int:
    count = 0
    count += len(re.findall(r"\bif\b", cleaned_line))
    count += len(re.findall(r"\bfor\b", cleaned_line))
    count += len(re.findall(r"\bwhile\b", cleaned_line))
    count += len(re.findall(r"\bcase\b", cleaned_line))
    count += cleaned_line.count("&&")
    count += cleaned_line.count("||")
    count += cleaned_line.count("?")
    return count


def build_tags(fn: FunctionInfo) -> List[str]:
    tags: List[str] = []
    if fn.length >= 80:
        tags.append("LONG")
    if fn.decision_count >= 18:
        tags.append("BRANCHY")
    if fn.max_nesting_depth >= 4:
        tags.append("DEEP_NEST")
    if fn.max_loop_depth >= 2:
        tags.append("NESTED_LOOPS")
    if fn.allocation_calls >= 3:
        tags.append("ALLOC_HEAVY")
    if fn.return_count >= 5:
        tags.append("MANY_RETURNS")
    if fn.parameter_count >= 6:
        tags.append("MANY_PARAMS")
    if not fn.is_static:
        tags.append("PUBLIC")
    return tags


def enrich_function_metrics(functions: List[FunctionInfo], src_root: Path) -> None:
    file_cache: Dict[str, List[str]] = {}
    alloc_pattern = re.compile(r"\b(" + "|".join(ALLOC_FUNCTIONS) + r")\s*\(")

    for fn in functions:
        if fn.file_path not in file_cache:
            file_cache[fn.file_path] = (src_root / fn.file_path).read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()

        lines = file_cache[fn.file_path]
        start_idx = max(0, fn.start_line - 1)
        end_idx = min(len(lines), fn.end_line)

        depth = 0
        max_depth = 0
        max_loop_depth = 0
        active_loop_depths: List[int] = []
        pending_loops = 0

        decisions = 0
        allocs = 0
        returns = 0

        in_block_comment = False

        for raw in lines[start_idx:end_idx]:
            cleaned, in_block_comment = strip_c_line(raw, in_block_comment)

            decisions += decision_count(cleaned)
            allocs += len(alloc_pattern.findall(cleaned))
            returns += len(re.findall(r"\breturn\b", cleaned))

            pending_loops += len(re.findall(r"\bfor\b", cleaned))
            pending_loops += len(re.findall(r"\bwhile\b", cleaned))
            pending_loops += len(re.findall(r"\bdo\b", cleaned))

            for ch in cleaned:
                if ch == "{":
                    depth += 1
                    if depth > max_depth:
                        max_depth = depth

                    if pending_loops > 0:
                        active_loop_depths.append(depth)
                        pending_loops -= 1

                    if len(active_loop_depths) > max_loop_depth:
                        max_loop_depth = len(active_loop_depths)

                elif ch == "}":
                    while active_loop_depths and active_loop_depths[-1] == depth:
                        active_loop_depths.pop()
                    depth = max(0, depth - 1)

            if pending_loops > 0 and "{" not in cleaned and ";" in cleaned:
                pending_loops = 0

        fn.decision_count = decisions
        fn.complexity_score = 1 + decisions
        fn.max_nesting_depth = max(0, max_depth - 1)  # exclude outer function body brace.
        fn.max_loop_depth = max_loop_depth
        fn.allocation_calls = allocs
        fn.return_count = returns

        fn.risk_score = int(
            round(
                1.5 * fn.length
                + 2.0 * fn.decision_count
                + 3.0 * fn.max_nesting_depth
                + 4.0 * fn.max_loop_depth
                + 3.0 * fn.allocation_calls
                + 1.0 * fn.return_count
                + 1.5 * fn.parameter_count
                + (1.0 if not fn.is_static else 0.0)
            )
        )


def collect_header_declared_functions(src_root: Path) -> Set[str]:
    exported: Set[str] = set()
    header_paths = [p for p in discover_source_files(src_root) if p.suffix in {".h", ".hpp"}]

    for path in header_paths:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        in_block_comment = False
        decl_lines: List[str] = []

        for raw in lines:
            cleaned, in_block_comment = strip_c_line(raw, in_block_comment)
            stripped = cleaned.strip()

            if not stripped or stripped.startswith("#"):
                continue

            decl_lines.append(cleaned)

            if "{" in cleaned:
                # inline/body content in header is not an exported declaration candidate here.
                decl_lines = []
                continue

            if ";" in cleaned:
                signature_text = "\n".join(decl_lines)
                function_name = extract_function_name(signature_text)
                if function_name and not is_static_function(signature_text):
                    exported.add(function_name)
                decl_lines = []

    return exported


def collect_call_site_files(source_files: List[Path], src_root: Path) -> Dict[str, Set[str]]:
    call_sites: Dict[str, Set[str]] = {}
    call_pattern = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")

    for path in source_files:
        rel_path = str(path.relative_to(src_root))
        in_block_comment = False
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            cleaned, in_block_comment = strip_c_line(raw, in_block_comment)
            for match in call_pattern.finditer(cleaned):
                name = match.group(1)
                call_sites.setdefault(name, set()).add(rel_path)

    return call_sites


def find_static_discipline_candidates(
    functions: List[FunctionInfo],
    exported_symbols: Set[str],
    call_site_files: Dict[str, Set[str]],
) -> List[StaticCandidate]:
    candidates: List[StaticCandidate] = []

    for fn in functions:
        if fn.is_static:
            continue
        if fn.name == "main":
            continue
        if not (fn.file_path.endswith(".c") or fn.file_path.endswith(".cpp")):
            continue
        if fn.name in exported_symbols:
            continue

        refs = call_site_files.get(fn.name, set())
        if refs == {fn.file_path}:
            candidates.append(
                StaticCandidate(
                    module=fn.module,
                    file_path=fn.file_path,
                    function_name=fn.name,
                    line_no=fn.start_line,
                    reason="used only in defining file and not declared in headers",
                )
            )

    candidates.sort(key=lambda c: (c.module, c.file_path, c.line_no, c.function_name))
    return candidates


def find_const_discipline_candidates(functions: List[FunctionInfo], src_root: Path) -> List[ConstCandidate]:
    file_cache: Dict[str, List[str]] = {}
    candidates: List[ConstCandidate] = []

    for fn in functions:
        if not fn.parameters:
            continue

        if fn.file_path not in file_cache:
            file_cache[fn.file_path] = (
                (src_root / fn.file_path)
                .read_text(encoding="utf-8", errors="replace")
                .splitlines()
            )

        lines = file_cache[fn.file_path]
        start_idx = max(0, fn.start_line - 1)
        end_idx = min(len(lines), fn.end_line)

        cleaned_lines: List[str] = []
        in_block_comment = False
        for raw in lines[start_idx:end_idx]:
            cleaned, in_block_comment = strip_c_line(raw, in_block_comment)
            cleaned_lines.append(cleaned)

        for param in fn.parameters:
            if not param.is_pointer or param.has_const or not param.name:
                continue
            if any(keyword in param.raw for keyword in IGNORED_CONST_TYPE_KEYWORDS):
                continue

            escaped = re.escape(param.name)
            mutation_patterns = [
                re.compile(rf"\*\s*{escaped}\s*="),
                re.compile(rf"\b{escaped}\s*\[[^\]]*\]\s*="),
                re.compile(rf"\b{escaped}\s*->\s*[A-Za-z_][A-Za-z0-9_]*\s*="),
            ]

            mutates_pointee = False
            for cl in cleaned_lines:
                if any(p.search(cl) for p in mutation_patterns):
                    mutates_pointee = True
                    break

            if not mutates_pointee:
                candidates.append(
                    ConstCandidate(
                        module=fn.module,
                        file_path=fn.file_path,
                        function_name=fn.name,
                        line_no=fn.start_line,
                        parameter_name=param.name,
                        parameter_raw=param.raw,
                    )
                )

    candidates.sort(key=lambda c: (c.module, c.file_path, c.line_no, c.function_name, c.parameter_name))
    return candidates


def find_enclosing_function(functions: List[FunctionInfo], line_no: int) -> str:
    for fn in functions:
        if fn.start_line <= line_no <= fn.end_line:
            return fn.name
    return "<global>"


def load_unsafe_rules(path: Path) -> List[UnsafeRule]:
    if not path.exists():
        return []

    rules: List[UnsafeRule] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue

        unsafe = ""
        safe = ""
        comment = ""

        if stripped.startswith("|"):
            # Markdown table format: | unsafe | safe | comment |
            cells = [c.strip() for c in stripped.strip("|").split("|")]
            if len(cells) < 2:
                continue
            if re.fullmatch(r"-+", cells[0]) or cells[0].lower() == "unsafe":
                continue
            unsafe = cells[0]
            safe = cells[1]
            if len(cells) >= 3:
                comment = cells[2].lstrip("#").strip()
        else:
            # Legacy TSV format: unsafe<TAB>safe<TAB># comment
            line_part = raw
            if "#" in raw:
                line_part, comment = raw.split("#", 1)
                comment = comment.strip()

            parts = [p.strip() for p in line_part.split("\t") if p.strip()]
            if len(parts) < 2:
                continue
            unsafe = parts[0]
            safe = parts[1]

        if not unsafe or not safe:
            continue

        pattern = re.compile(r"\b" + re.escape(unsafe) + r"\s*\(")
        rules.append(UnsafeRule(unsafe=unsafe, safe=safe, comment=comment, pattern=pattern))

    return rules


def scan_unsafe_usage(
    source_files: List[Path],
    src_root: Path,
    functions: List[FunctionInfo],
    rules: List[UnsafeRule],
) -> List[UnsafeHit]:
    by_file_functions: Dict[str, List[FunctionInfo]] = {}
    for fn in functions:
        by_file_functions.setdefault(fn.file_path, []).append(fn)
    for file_path in by_file_functions:
        by_file_functions[file_path].sort(key=lambda f: (f.start_line, f.end_line))

    hits: List[UnsafeHit] = []

    for path in source_files:
        rel_path = str(path.relative_to(src_root))
        module = module_from_path(path, src_root)
        file_functions = by_file_functions.get(rel_path, [])

        in_block_comment = False
        for idx, raw in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            cleaned, in_block_comment = strip_c_line(raw, in_block_comment)
            for rule in rules:
                if rule.pattern.search(cleaned):
                    hits.append(
                        UnsafeHit(
                            module=module,
                            file_path=rel_path,
                            line_no=idx,
                            unsafe=rule.unsafe,
                            safe=rule.safe,
                            comment=rule.comment,
                            line_text=raw.strip(),
                            function_name=find_enclosing_function(file_functions, idx),
                        )
                    )

    hits.sort(key=lambda h: (h.module, h.file_path, h.line_no, h.unsafe))
    return hits


def get_git_metadata(start_dir: Path) -> Tuple[str, str]:
    try:
        branch = (
            subprocess.check_output(
                ["git", "-C", str(start_dir), "rev-parse", "--abbrev-ref", "HEAD"],
                text=True,
                stderr=subprocess.DEVNULL,
            )
            .strip()
        )
        commit = (
            subprocess.check_output(
                ["git", "-C", str(start_dir), "rev-parse", "HEAD"],
                text=True,
                stderr=subprocess.DEVNULL,
            )
            .strip()
        )
        return branch, commit
    except Exception:
        return "unknown", "unknown"


def md_escape(value: str) -> str:
    return value.replace("|", "\\|")


def write_spotlight_report(
    out_path: Path,
    functions: List[FunctionInfo],
    static_candidates: List[StaticCandidate],
    const_candidates: List[ConstCandidate],
    branch: str,
    commit: str,
) -> None:
    by_module: Dict[str, List[FunctionInfo]] = {}
    for fn in functions:
        by_module.setdefault(fn.module, []).append(fn)

    module_names = sorted(by_module.keys())
    for module in module_names:
        by_module[module].sort(key=lambda x: (-x.length, x.file_path, x.start_line))

    top3_longest = sorted(functions, key=lambda x: (-x.length, x.module, x.file_path, x.start_line))[:3]
    top10_risk = sorted(
        functions,
        key=lambda x: (-x.risk_score, -x.length, x.module, x.file_path, x.start_line),
    )[:10]

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as f:
        f.write("# Sunspots Code Spotlight Report\n\n")
        f.write(f"- Generated: `{dt.datetime.now().isoformat(timespec='seconds')}`\n")
        f.write(f"- Branch: `{md_escape(branch)}`\n")
        f.write(f"- Commit: `{md_escape(commit)}`\n\n")
        f.write("## Legend\n\n")
        f.write(
            "- `Len`: function length in lines, `Dec`: decision count, "
            "`Nest`: max nesting depth, `Loops`: max loop depth, "
            "`Alloc`: allocation/free call count, `Returns`: return statements, "
            "`Params`: parameter count\n\n"
        )

        f.write("## High Score (Top 3 Longest Functions)\n\n")
        if not top3_longest:
            f.write("No functions detected.\n")
        else:
            f.write("| Rank | Function | Length | Module | Location |\n")
            f.write("|---:|---|---:|---|---|\n")
            for rank, fn in enumerate(top3_longest, start=1):
                f.write(
                    f"| {rank} | `{md_escape(fn.name)}` | {fn.length} | `{md_escape(fn.module)}` | "
                    f"`{md_escape(fn.file_path)}:{fn.start_line}-{fn.end_line}` |\n"
                )
        f.write("\n")

        f.write("## Low-Hanging Fruit Spotlight (Top 10 by Risk Score)\n\n")
        if not top10_risk:
            f.write("No functions detected.\n")
        else:
            f.write(
                "| Rank | Len | Dec | Nest | Loops | Alloc | Returns | Params | Module | Function | Tags |\n"
            )
            f.write("|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|\n")
            for rank, fn in enumerate(top10_risk, start=1):
                tags = ",".join(build_tags(fn))
                f.write(
                    f"| {rank} | {fn.length} | {fn.decision_count} | "
                    f"{fn.max_nesting_depth} | {fn.max_loop_depth} | {fn.allocation_calls} | "
                    f"{fn.return_count} | {fn.parameter_count} | `{md_escape(fn.module)}` | "
                    f"`{md_escape(fn.name)}` (`{md_escape(fn.file_path)}:{fn.start_line}-{fn.end_line}`) | "
                    f"`{md_escape(tags)}` |\n"
                )
        f.write("\n")

        f.write("## Function Lengths Per Module\n")
        for module in module_names:
            f.write(f"\n### `{md_escape(module)}`\n")
            module_functions = by_module[module]
            if not module_functions:
                f.write("(no functions detected)\n")
                continue

            avg_len = sum(fn.length for fn in module_functions) / float(len(module_functions))
            f.write(f"- Function count: **{len(module_functions)}**\n")
            f.write(f"- Average length: **{avg_len:.2f}** lines\n")
            f.write("\n")
            f.write("| Length | Location | Function |\n")
            f.write("|---:|---|---|\n")
            for fn in module_functions:
                f.write(
                    f"| {fn.length} | `{md_escape(fn.file_path)}:{fn.start_line}-{fn.end_line}` | "
                    f"`{md_escape(fn.name)}` |\n"
                )

        f.write("\n## Spotlight Per Module (Top 5 by Risk Score)\n")
        for module in module_names:
            f.write(f"\n### `{md_escape(module)}`\n")
            module_functions = sorted(
                by_module[module],
                key=lambda x: (-x.risk_score, -x.length, x.file_path, x.start_line),
            )
            if not module_functions:
                f.write("(no functions detected)\n")
                continue

            f.write(f"- Function count: **{len(module_functions)}**\n")
            f.write("\n")
            f.write("| Len | Dec | Nest | Loops | Function | Tags |\n")
            f.write("|---:|---:|---:|---:|---|---|\n")
            for fn in module_functions[:5]:
                tags = ",".join(build_tags(fn))
                f.write(
                    f"| {fn.length} | {fn.decision_count} | "
                    f"{fn.max_nesting_depth} | {fn.max_loop_depth} | "
                    f"`{md_escape(fn.name)}` (`{md_escape(fn.file_path)}:{fn.start_line}-{fn.end_line}`) | "
                    f"`{md_escape(tags)}` |\n"
                )

        f.write("\n## Static Discipline Candidates\n\n")
        if not static_candidates:
            f.write("No obvious `static` candidates detected.\n")
        else:
            f.write("| Module | Location | Function | Reason |\n")
            f.write("|---|---|---|---|\n")
            for cand in static_candidates:
                f.write(
                    f"| `{md_escape(cand.module)}` | "
                    f"`{md_escape(cand.file_path)}:{cand.line_no}` | "
                    f"`{md_escape(cand.function_name)}` | "
                    f"{md_escape(cand.reason)} |\n"
                )

        f.write("\n## Const Discipline Candidates\n\n")
        if not const_candidates:
            f.write("No obvious `const` pointer-parameter candidates detected.\n")
        else:
            f.write("| Module | Location | Function | Parameter | Suggested Direction |\n")
            f.write("|---|---|---|---|---|\n")
            for cand in const_candidates:
                f.write(
                    f"| `{md_escape(cand.module)}` | "
                    f"`{md_escape(cand.file_path)}:{cand.line_no}` | "
                    f"`{md_escape(cand.function_name)}` | "
                    f"`{md_escape(cand.parameter_name)}` (`{md_escape(cand.parameter_raw)}`) | "
                    f"consider `const` on pointee |\n"
                )


def write_unsafe_report(
    out_path: Path, rules: List[UnsafeRule], hits: List[UnsafeHit], branch: str, commit: str
) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as f:
        f.write("# Sunspots Unsafe API Usage Report\n\n")
        f.write(f"- Generated: `{dt.datetime.now().isoformat(timespec='seconds')}`\n")
        f.write(f"- Branch: `{md_escape(branch)}`\n")
        f.write(f"- Commit: `{md_escape(commit)}`\n\n")
        if not hits:
            f.write("No unsafe API uses detected from current rules.\n")
            return

        grouped: Dict[str, Dict[str, List[UnsafeHit]]] = {}
        for hit in hits:
            grouped.setdefault(hit.module, {}).setdefault(hit.file_path, []).append(hit)

        f.write("## Detailed Hits\n")
        for module in sorted(grouped.keys()):
            f.write(f"\n### `{md_escape(module)}`\n")
            for file_path in sorted(grouped[module].keys()):
                f.write(f"\n#### `{md_escape(file_path)}`\n")
                f.write("| Line | Function | Unsafe | Safe Alternative | Comment |\n")
                f.write("|---:|---|---|---|---|\n")
                for hit in grouped[module][file_path]:
                    comment = md_escape(hit.comment) if hit.comment else ""
                    f.write(
                        f"| {hit.line_no} | `{md_escape(hit.function_name)}` | "
                        f"`{md_escape(hit.unsafe)}` | `{md_escape(hit.safe)}` | {comment} |\n"
                    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate code spotlight reports.")
    parser.add_argument("--src", default="src", help="Source root directory (default: src)")
    parser.add_argument(
        "--out",
        default="scripts/check_code_out.md",
        help="Spotlight output report path (default: scripts/check_code_out.md)",
    )
    parser.add_argument(
        "--unsafe-rules",
        default="scripts/unsafe.md",
        help="Unsafe API rules path (default: scripts/unsafe.md)",
    )
    parser.add_argument(
        "--unsafe-out",
        default="scripts/check_code_unsafe_out.md",
        help="Unsafe API usage report path (default: scripts/check_code_unsafe_out.md)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    src_root = Path(args.src).resolve()
    out_path = Path(args.out).resolve()
    unsafe_rules_path = Path(args.unsafe_rules).resolve()
    unsafe_out_path = Path(args.unsafe_out).resolve()

    if not src_root.exists() or not src_root.is_dir():
        raise SystemExit(f"Source directory not found: {src_root}")

    source_files = discover_source_files(src_root)
    functions: List[FunctionInfo] = []
    branch, commit = get_git_metadata(src_root)

    for path in source_files:
        module = module_from_path(path, src_root)
        functions.extend(find_functions_in_file(path, module, src_root))

    enrich_function_metrics(functions, src_root)
    exported_symbols = collect_header_declared_functions(src_root)
    call_site_files = collect_call_site_files(source_files, src_root)
    static_candidates = find_static_discipline_candidates(functions, exported_symbols, call_site_files)
    const_candidates = find_const_discipline_candidates(functions, src_root)
    write_spotlight_report(
        out_path,
        functions,
        static_candidates,
        const_candidates,
        branch,
        commit,
    )

    rules = load_unsafe_rules(unsafe_rules_path)
    hits = scan_unsafe_usage(source_files, src_root, functions, rules)
    write_unsafe_report(unsafe_out_path, rules, hits, branch, commit)

    print(f"Wrote spotlight report: {out_path}")
    print(f"Wrote unsafe API report: {unsafe_out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
