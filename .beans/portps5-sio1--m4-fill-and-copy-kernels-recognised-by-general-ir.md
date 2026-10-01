---
# portps5-sio1
title: 'M4: fill and copy kernels recognised by general IR patterns'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:47Z
updated_at: 2026-10-01T18:27:12Z
parent: portps5-rd6g
---

## Context

ROADMAP M4 item (M4: fill and copy kernels recognised by general IR patterns). Owner spec: shader-recompiler.md, gpu-driver.md. Blocked by: none.

## Higher Goal

Close the ROADMAP M4 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] KernelIdiom recognises uniform fill and linear copy on synthetic IR, no title hashes
- [ ] Transfers byte-identical to running the shader (lavapipe)
- [ ] ROADMAP and shader-recompiler.md, gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M4 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
