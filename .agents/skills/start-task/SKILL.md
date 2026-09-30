---
name: start-task
description: Standard procedure to start a new task in PortPS5. Checks upstream AnyPS5 for reference code, creates an isolated git worktree, sets up the structured task plan, and establishes test baselines.
---

# Start a Task

When starting any new task, bugfix, or roadmap deliverable on PortPS5, execute this workflow in order:

## 1. Upstream & Remote Sync
1. Fetch latest remote branches and upstream:
   ```bash
   git fetch origin
   git fetch upstream
   ```
2. **Consult Upstream AnyPS5 (`https://github.com/boykopovar/AnyPS5`)**:
   - Check if upstream `main` or open PRs have relevant implementations, fixes, or tests for the target subsystem:
     ```bash
     git log -n 10 --oneline upstream/main -- <subsystem-paths>
     git diff origin/main..upstream/main -- <subsystem-paths>
     ```
   - Note useful patterns, data structures, or bug fixes to adapt.
   - **Never copy verbatim without adaptation:** PortPS5 strictly requires `APS5_VABI` on guest exports, no host exceptions crossing ABI boundaries (`noexcept` / SCE error codes), GTest suites (`portps5_add_gtest`), no title-specific hacks, and comprehensive code comments explaining "why, not what".

## 2. Check Roadmap & Subsystem Spec
1. Consult `docs/ROADMAP.md` to confirm the milestone scope and exit criteria.
2. Read the subsystem spec in `docs/spec/<subsystem>.md` (Target design, Decisions, Interfaces, Failure modes).
3. Run `beans list` (or `beans list --json --ready`) to find an existing bean for this work; create one with `beans create` if none exists (see AGENTS.md "Task tracking (beans)").

## 3. Create Worktree & Branch
1. Ensure the base is up to date with `origin/main`:
   ```bash
   git checkout -b <branch-name> origin/main
   ```
   Or create a new dedicated worktree:
   ```bash
   git worktree add <worktree-path> -b <branch-name> origin/main
   ```

## 4. Structure the Task Plan
Standardize task specification following Rule 10:
- **Context:** Root problem, motivation, and current state.
- **Higher Goal:** Architectural intent and systemic benefits.
- **Acceptance Criteria:** Concrete, verifiable deliverables using checkboxes (`- [ ]` / `- [x]`).
- **Out of Scope:** Explicit boundaries and deferred items.
- **Summary of Changes:** Specific files, implementations, tests, or docs modified.

## 5. Verify Build & Test Baseline
Run existing tests before writing new changes using the `build-and-test` skill:
```bash
ctest --preset ci
```
Ensure working tree is clean and baseline passes.
