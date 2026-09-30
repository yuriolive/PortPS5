# AGENTS.md

This is the canonical instruction file for AI coding agents working on PortPS5: Claude Code, opencode and Google Antigravity. Tool-specific files only point here. Edit this file and `.agents/`, never a copy.

## Project

PortPS5 converts **user-supplied, already-decrypted PS5 game dumps** into native Windows executables. The relinker rewrites the ELF into a PE image, replacement system libraries (`.prx`) implement the PS5 APIs, and GPU work is translated to Vulkan. There is no CPU emulation.

It is a GPL-2.0-only hard fork of [AnyPS5](https://github.com/boykopovar/AnyPS5).

## Sources of truth

Read these before changing behaviour. If code and spec disagree, raise it; don't silently pick one.

| Topic | File |
|---|---|
| Product goals, 1.0 bar, scope, risks | [docs/PRD.md](docs/PRD.md) |
| Milestones and exit criteria | [docs/ROADMAP.md](docs/ROADMAP.md) |
| Subsystem specs (one file per subsystem) | [docs/spec/README.md](docs/spec/README.md) |
| CI, local regression, results JSON | [docs/spec/verification.md](docs/spec/verification.md) |

## Rules

The detailed rules live in `.agents/rules/`, and all of them apply:

- [.agents/rules/legal-boundary.md](.agents/rules/legal-boundary.md): what may never enter the repo, CI or issues.
- [.agents/rules/no-title-hacks.md](.agents/rules/no-title-hacks.md): no title-specific code in core subsystems.
- [.agents/rules/cpp-style.md](.agents/rules/cpp-style.md): C++ and ABI conventions.
- [.agents/rules/testing.md](.agents/rules/testing.md): what to test and where it runs.
- [.agents/rules/docs-and-specs.md](.agents/rules/docs-and-specs.md): keeping specs in sync, and how docs refer to hardware.
- [.agents/rules/git-workflow.md](.agents/rules/git-workflow.md): branches, commits, PRs.

Short version:

1. Never add keys, firmware, Sony libraries or SDK headers, decryption code, or game data (dumps, shader bytecode, saves, frames, recorded input) to the repo, CI, issues or PRs.
2. No title-specific code paths in `core/`. Per-title behaviour goes only in `config/games/<titleId>.toml`, as a documented mechanism.
3. Host exports called by guest code use `APS5_VABI` (System V ABI). A missing attribute corrupts registers silently.
4. Real POSIX/SCE errors return codes. Only truly unsupported states abort, through the logging abort path.
5. A change to a subsystem's behaviour updates its spec in `docs/spec/` in the same PR.
6. Docs describe hardware only as the generic reference tier. Never name a specific personal machine.
7. Enforce code comments across all new files, public interfaces, and test fixtures: document file purpose, non-obvious rationale, ABI invariants, and workarounds inline in the code. Iterate and verify builds and unit tests locally before committing.
8. Every issue, bug report, or review finding is a potential test: always rigorously verify whether a review finding or bot comment is technically correct before acting on it (never apply changes blindly). Once verified, always add a unit or regression test for any edge case, race condition, or bug identified during review or development.
9. Maintain task checkboxes across `docs/ROADMAP.md` and `docs/spec/`. As tasks, roadmap milestones, or subsystem spec items are completed, mark them as finished (`- [x]`) in the same PR. Keep pending items checked as open (`- [ ]`).
10. Standardize task plans and PR descriptions. Every task, unit of work, or issue (including beans tasks and plans) must follow this structured specification:
   - **Context:** Root problem, motivation, and current state.
   - **Higher Goal:** Architectural intent and systemic benefits.
   - **Acceptance Criteria:** Concrete, verifiable deliverables using checkboxes (`- [ ]` / `- [x]`).
   - **Out of Scope:** Explicit boundaries and deferred items.
   - **Summary of Changes:** Specific files, implementations, tests, or docs modified.
10. Mandatory GoogleTest (GTest + GMock) for all C++ tests via `portps5_add_gtest`. Never write ad-hoc C++ test runners using bare `abort()`, custom `Require()`, or manual `main()` functions. Always use standard GoogleTest assertions (`EXPECT_*` / `ASSERT_*`) and GTest discovery.

## Task tracking (beans)

Tasks live as markdown files in `.beans/` (tool: [hmans/beans](https://github.com/hmans/beans), CLI `beans`, config `.beans.yml`). Beads (`bd`, `.beads/`) is retired; don't use it. Run `beans prime` at session start and follow its output. Use beans, not TodoWrite or ad-hoc todo lists.

- **Before work:** find or create the bean (`beans create "Title" -t task -s in-progress -d ...`). Its body follows rule 10 (Context, Higher Goal, Acceptance Criteria, Out of Scope, Summary of Changes).
- **Every PR closes the bean(s) it solves.** In the same PR: tick the acceptance checkboxes, add `## Summary of Changes`, set `-s completed`, and run `beans archive` so the bean file moves to `.beans/archive/`. After merge to `main`, the tree then shows the task as done. Name the bean ID(s) in the PR description.
- Work deferred or found during the PR gets a new open bean, not a silent TODO.
- Scrapped work: set `-s scrapped` with a `## Reasons for Scrapping` section, then archive.
- Commit bean files together with the code change they track.

## Skills

Reusable procedures live in `.agents/skills/<name>/SKILL.md`. opencode and Antigravity read that folder directly, and Claude Code reads it through the `.claude/skills` symlink.

| Skill | Use when |
|---|---|
| `build-and-test` | Configuring, building or running tests |
| `implement-prx-function` | Implementing or fixing a PS5 system-library function |
| `update-spec` | Changing subsystem behaviour, or checking docs acceptance checks |
| `milestone-seed` | Starting work on a ROADMAP milestone |
| `compat-result` | Recording a local title test run as results JSON |

## Layout

The code arrives with the fork at Milestone 0 and follows the AnyPS5 layout:

```
core/relinker/        ELF -> PE relinker, NID resolution, --to-intel
core/libs/prx/<lib>/  replacement system libraries (libc, libkernel, libSceAgcDriver, ...)
core/shader/          RDNA2 -> SSA IR -> SPIR-V recompiler
config/               global.toml, games/<titleId>.toml
docs/                 PRD, ROADMAP, spec/
.agents/              rules/ and skills/ shared by all AI tools
```

## Toolchain

- C++ (C++20 today; moving to C++23 in Milestone 0) with CMake 3.20 or later.
- MinGW-w64 GCC 15.2 (winlibs, ucrt-posix-seh) is the only supported Windows compiler. MSVC cannot express `sysv_abi`, so don't propose it.
- Vulkan backend, with SDL2 for windowing, audio and input.

## Landing the Plane (Session Completion)

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update beans** - Tick acceptance checkboxes, mark finished beans `completed` and run `beans archive` (see Task tracking). Commit the `.beans/` changes with the code
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
