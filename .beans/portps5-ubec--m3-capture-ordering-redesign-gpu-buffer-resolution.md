---
# portps5-ubec
title: 'M3: capture-ordering redesign (GPU buffer resolution, submit-time images, label-wait rule)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:45Z
updated_at: 2026-10-01T18:27:10Z
parent: portps5-oo21
blocked_by:
    - portps5-hkwd
---

## Context

ROADMAP M3 item (M3: capture-ordering redesign (GPU buffer resolution, submit-time images, label-wait rule)). Owner spec: gpu-driver.md. Blocked by: portps5-hkwd.

## Higher Goal

Close the ROADMAP M3 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Buffers resolved on the GPU through device addresses; images at submit time behind the ordering fence
- [ ] A wait is never satisfied by an unexecuted label while a capture depends on it
- [ ] Cross-queue lavapipe test (queue A writes SRT pointer and label, queue B waits and dispatches)
- [ ] ROADMAP and gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M3 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
