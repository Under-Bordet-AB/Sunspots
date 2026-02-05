---
description: Perform a focused code review against SUNSPOTS standards, emphasizing issues static tools may miss.
---

# Review Mode
Be precise, practical, and evidence-driven. Prefer concrete bugs, regressions, and standards violations over style opinions.

## Core Rules

- Review behavior and maintainability, not personal taste.
- Every finding must include: severity, evidence, impact, and suggested fix.
- Cite the exact relevant section in `docs/standards/code.md` when reporting a standards violation.
- If no findings are present, explicitly state that.

## Scope

- Include: `src/`, `include/`
- Exclude: `src/libs/`

## Workflow

1. Build and checks gate:
   - `make debug`
   - `clang-tidy` (project-configured run)
   - Formatting check (`clang-format --dry-run --Werror` or project equivalent)
2. If any gate fails, stop and report gate failures first. Do not continue to semantic review.
3. Read `docs/standards/code.md` and `docs/standards/banned.md`.
4. Review code for issues tools commonly miss:
   - API contract mismatches
   - Ownership/lifecycle bugs and cleanup-path leaks
   - Error propagation inconsistencies (`0` / negative errno / `NULL`)
   - Missing input validation at public boundaries
   - Incorrect `const` usage for read-only pointers
   - Non-`static` internal symbols
   - Naming/signature patterns that violate the standard
5. Write report to `report_review.md` in project root.

## Report Format (`report_review.md`)

Order findings by severity: Critical -> High -> Medium -> Low.

Use this table format:

| Field | Value |
|---|---|
| Severity | High |
| Rule | `docs/standards/code.md` (section + short quote/paraphrase) |
| File | `[server.c](src/server.c#L419)` |
| Evidence | What is wrong, concretely |
| Impact | What can break and when |
| Recommendation | Smallest safe fix |

## Link Rules

- Use relative links from project root only.
- Correct: `[server.c](src/server.c#L419)`, `[code.md](docs/standards/code.md#L6)`
- Wrong: `[server.c](file:///home/user/project/src/server.c#L419)`
