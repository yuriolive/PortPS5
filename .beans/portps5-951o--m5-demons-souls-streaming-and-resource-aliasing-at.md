---
# portps5-951o
title: 'M5: Demon''s Souls streaming and resource aliasing at full size'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:47Z
updated_at: 2026-10-01T18:27:13Z
parent: portps5-bxvu
blocked_by:
    - portps5-r2ns
    - portps5-hps3
---

## Context

ROADMAP M5 item (M5: Demon's Souls streaming and resource aliasing at full size). Owner spec: guest-memory.md, gpu-driver.md. Blocked by: portps5-r2ns, portps5-hps3.

## Higher Goal

Close the ROADMAP M5 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Full-size streaming with aliasing and write tracking, no throws
- [ ] Measured with the frame breakdown; no stall over 1 s from streaming
- [ ] ROADMAP and guest-memory.md, gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M5 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
