---
# portps5-273j
title: 'M3 exit: Demon''s Souls intro stress run, 3 x 150 s, 0 wedges, 0 not-readable skips'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:26:46Z
updated_at: 2026-10-01T18:27:11Z
parent: portps5-oo21
blocked_by:
    - portps5-ubec
    - portps5-c08m
---

## Context

ROADMAP M3 item (M3 exit: Demon's Souls intro stress run, 3 x 150 s, 0 wedges, 0 not-readable skips). Owner spec: gpu-driver.md. Blocked by: portps5-ubec, portps5-c08m.

## Higher Goal

Close the ROADMAP M3 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Three 150 s runs of the intro cinematic with 0 wedges
- [ ] 0 skipped dispatches from the guest-memory-not-readable class
- [ ] Recorded as results JSON only
- [ ] ROADMAP and gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M3 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
