---
# portps5-cexw
title: 'M4: Wave64 through VK_EXT_subgroup_size_control'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:46Z
updated_at: 2026-10-01T18:27:12Z
parent: portps5-rd6g
blocked_by:
    - portps5-l77s
---

## Context

ROADMAP M4 item (M4: Wave64 through VK_EXT_subgroup_size_control). Owner spec: shader-recompiler.md, gpu-driver.md. Blocked by: portps5-l77s.

## Higher Goal

Close the ROADMAP M4 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Required subgroup size set per pipeline stage where supported, rejected with a logged error otherwise
- [ ] Synthetic 64-lane compute dispatch validation on lavapipe
- [ ] ROADMAP and shader-recompiler.md, gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M4 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
