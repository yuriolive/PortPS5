---
# portps5-oq4m
title: 'M1 exit: golden corpus covers every decoded instruction class; local corpus replays with 0 failures'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:26:44Z
updated_at: 2026-10-01T18:27:09Z
parent: portps5-7dk3
blocked_by:
    - portps5-qxip
---

## Context

ROADMAP M1 item (M1 exit: golden corpus covers every decoded instruction class; local corpus replays with 0 failures). Owner spec: shader-recompiler.md. Blocked by: portps5-qxip.

## Higher Goal

Close the ROADMAP M1 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Hosted synthetic golden corpus has at least one shader per decoded instruction class (coverage report from tools/progress.py)
- [ ] Local game-derived corpus replays with 0 spirv-val failures, recorded only as a results JSON count
- [ ] ROADMAP and shader-recompiler.md checkboxes ticked in the same PR

## Out of Scope

Other M1 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
