---
# portps5-e6xg
title: Local nightly regression on the maintainer machine (scheduled, results JSON only)
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:01:25Z
updated_at: 2026-10-01T19:01:26Z
parent: portps5-7n6b
blocked_by:
    - portps5-3m3u
    - portps5-52bs
---

## Context

Self-hosted runners are not allowed on the public repo (verification.md section 1), so the game-run tier of other emulators' CI becomes a scheduled local job. Blocked by: portps5-3m3u (regress runner), portps5-52bs (perf scenes).

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] Windows scheduled task runs prepare, boot, perf scenes and compare for each pinned gate title on the latest main
- [ ] Locked environment: fixed power plan, no other load, GPU clocks locked where the vendor tool allows, run-to-run variance recorded
- [ ] Output is results JSON only (compat-result skill); nothing uploaded automatically
- [ ] Documented in verification.md section 2

## Out of Scope

Hosted game runs.

## Summary of Changes

TBD
