---
# portps5-y2k5
title: 'M3: GPU-side path for the DRAW_INDIRECT family, no CPU record reads'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:44Z
updated_at: 2026-10-01T18:27:10Z
parent: portps5-oo21
blocked_by:
    - portps5-hkwd
---

## Context

ROADMAP M3 item (M3: GPU-side path for the DRAW_INDIRECT family, no CPU record reads). Owner spec: gpu-driver.md. Blocked by: portps5-hkwd.

## Higher Goal

Close the ROADMAP M3 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Every indirect draw/dispatch form goes through vkCmd*Indirect[Count] fed by GPU-resolved arguments
- [ ] No CPU read of indirect records; an unsupported form aborts through Unsupported()
- [ ] lavapipe tests with synthetic PM4 for each form
- [ ] ROADMAP and gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M3 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
