# SUNSPOTS GitHub Workflow Standards

> **Version**: 1.0
> **Scope**: All Contributors (Agents & Developers)

## Core Principles

1.  **Traceability**: Every change must originate from an Issue.
2.  **Clean History**: The main branch history should be linear and clean. We squash commits before merging.
3.  **Consistency**: All Pull Requests (PRs) must follow the standard template inherited from the organization root.

## Workflow Overview

The standard lifecycle of a change is:

`Issue` -> `Branch` -> `Development` -> `Squash (Compress)` -> `Pull Request` -> `Merge`

---

## Detailed Steps

### 1. Start with an Issue
**Everything starts here.**
*   Do not start coding without an Issue.
*   The Issue defines the specific scope of work (Task, Bug, or Feature).
*   If you find a new bug while working, create a **new** Issue; do not bloat the current one.

### 2. Create a Branch
*   Create a branch directly linked to the Issue.
*   **Naming Convention**: `type/issue-id-short-desc`
    *   Examples:
        *   `feat/42-add-login`
        *   `fix/99-memleak-auth`
        *   `docs/101-update-readme`

### 3. Development Loop
*   Work in your branch.
*   Commit often locally.
*   Push to the remote branch to save work.
*   **Note**: At this stage, messy commits ("wip", "fixing typo") are acceptable.

### 4. Preparation (The Compress Step)
**Crucial Step**: Before opening a PR (or before the final merge), you must "compress" or squash your commits so the history remains clean.

*   Combine your "work in progress" commits into meaningful, atomic units.
*   Ideally, one feature = one commit (or a small series of logical commits).
*   **Why?** Use `git rebase -i` or squash merge strategies to ensure the `main` branch doesn't get cluttered with "fixed typo" commits.

### 5. Pull Request (PR)
*   Open a Pull Request ensuring it targets the correct base branch (usually `main`).
*   **Template**: You **MUST** use the provided Pull Request Template located in the organization root (`.github/pull_request_template.md`).
    *   *Do not ignore the template sections.*
    *   Link the PR to the Issue (e.g., "Closes #42").

### 6. Review & Merge
*   Wait for checks (CI/CD) to pass.s
*   Address review comments.
*   Merge into `main`.

---
# PR TEMPLATE
```markdown
## 🔗 Linked Issue
<!-- Replace X with the actual issue number (e.g., Closes #123) -->
<!-- This will auto-close the issue when the PR is merged -->
Closes #X

## 📝 Summary
*Briefly describe what this PR does and why.*

## 🚦 Testing Checklist (loop until all pass)
- [ ] **Start fresh:** `make clean`
- [ ] **Clang-Format:** `make format`
- [ ] **Clang-Tidy:** `make lint`
- [ ] **Built successfully:** `make debug`
- [ ] **Tests Passed:** `make test`
- [ ] **LLM Review:** `/review_code` workflow passed?

## 🏛️ Architectural Decisions
*Any significant design choices or trade-offs made?*
```