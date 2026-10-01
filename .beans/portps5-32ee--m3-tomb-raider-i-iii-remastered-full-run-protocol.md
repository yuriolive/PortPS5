---
# portps5-32ee
title: 'M3: Tomb Raider I-III Remastered full-run protocol (gate 3)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:26:46Z
updated_at: 2026-10-01T18:27:11Z
parent: portps5-oo21
blocked_by:
    - portps5-9s7e
    - portps5-hkwd
    - portps5-aifo
    - portps5-c08m
    - portps5-ubec
    - portps5-uag4
    - portps5-macl
---

## Context

ROADMAP M3 item (M3: Tomb Raider I-III Remastered full-run protocol (gate 3)). Owner spec: verification.md. Blocked by: portps5-9s7e, portps5-hkwd, portps5-aifo, portps5-c08m, portps5-ubec, portps5-uag4, portps5-macl.

## Higher Goal

Close the ROADMAP M3 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Full run meets the PRD 4.3 bar with save round-trip
- [ ] Steady-state P1 and P2 counters are 0, or each nonzero counter has a bean (performance track)
- [ ] No game data committed
- [ ] ROADMAP and verification.md checkboxes ticked in the same PR

## Out of Scope

Other M3 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
