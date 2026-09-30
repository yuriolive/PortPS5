# Git workflow

- **Branches:** `main` is protected. Work on `feat/…`, `fix/…`, `docs/…` or `chore/…` branches, one concern per branch.
- **Commits:** Conventional Commits (`feat(agc): …`, `fix(libkernel): …`, `docs(spec): …`). The subject is at most 72 characters; the body explains why.
- **Local Verification Before Commit:**
  - Always iterate and verify changes locally (clean compile and test run) before creating a commit or pushing to remote. Do not rely on hosted CI as a syntax/link checker.
  - Local worktrees share `ccache` at `%LOCALAPPDATA%\ccache` across the machine, keeping rebuilds across branches and worktrees down to seconds.
- **PRs and Tasks:**
  - Structure PR descriptions, task plans, and issue tracking (including beans issues and tasks) with: Context, Higher Goal, Acceptance Criteria (`- [ ]` / `- [x]`), Out of Scope, and Summary of Changes.
  - **Bean hygiene:** every PR must check off and archive the bean(s) it solves, in the same PR, so `main` reflects reality after merge: tick acceptance checkboxes, add `## Summary of Changes`, `beans update <id> -s completed`, `beans archive`. List the bean IDs in the PR description. A PR that solves a task but leaves its bean open is not ready to merge.
  - Say which ROADMAP milestone and which spec files the PR touches, and mark finished checkboxes as done (`- [x]`).
  - List the tests you added and how you verified the change.
  - Never attach game footage, logs containing game data, or dumps.
- **Upstream (AnyPS5):**
  - It is not tracked for now.
  - When you port something from AnyPS5 or its PRs, cite the source commit in the commit body.
- **Generated or large files:**
  - Don't commit build outputs, caches or telemetry logs.
  - Check `.gitignore` before adding new output paths.
