---
# portps5-zqk2
title: 'M4: GPU-side descriptor heap for bindless'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:47Z
updated_at: 2026-10-01T18:27:12Z
parent: portps5-rd6g
blocked_by:
    - portps5-hkwd
    - portps5-l77s
    - portps5-7li6
---

## Context

ROADMAP M4 item (M4: GPU-side descriptor heap for bindless). Owner spec: gpu-driver.md. Blocked by: portps5-hkwd, portps5-l77s, portps5-7li6.

## Higher Goal

Close the ROADMAP M4 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Persistent descriptors indexed by resource ID through VK_EXT_descriptor_indexing (descriptor_buffer decided by gpu-driver open question 5)
- [ ] No per-draw descriptor allocation (invariant P2)
- [ ] Descriptor-heap probe and exhaustion tests
- [ ] ROADMAP and gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M4 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
