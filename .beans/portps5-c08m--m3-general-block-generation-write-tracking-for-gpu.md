---
# portps5-c08m
title: 'M3: general block-generation write tracking for GPU-written surfaces'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:45Z
updated_at: 2026-10-01T18:27:10Z
parent: portps5-oo21
blocked_by:
    - portps5-421p
    - portps5-hkwd
---

## Context

ROADMAP M3 item (M3: general block-generation write tracking for GPU-written surfaces). Owner spec: gpu-driver.md, video-fmv.md. Blocked by: portps5-421p, portps5-hkwd.

## Higher Goal

Close the ROADMAP M3 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] GPU writes call MarkWritten on the tracker; readers compare generations
- [ ] Replaces the interim adjacent block-generation advance (portps5-ux18)
- [ ] Unit and lavapipe tests for write-then-read across queues
- [ ] ROADMAP and gpu-driver.md, video-fmv.md checkboxes ticked in the same PR

## Out of Scope

Other M3 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
