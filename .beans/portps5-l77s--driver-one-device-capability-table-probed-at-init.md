---
# portps5-l77s
title: 'Driver: one device capability table probed at init'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:12:42Z
updated_at: 2026-10-01T18:15:26Z
parent: portps5-epoi
---

## Context

PR #62 raised the floor to Vulkan 1.3 and probes features at device creation; subgroup-size control (M4), the descriptor model (M4) and 2.0 ray tracing and mesh shaders need one place that answers 'is this supported'. Blocked by: none.

## Higher Goal

Every capability-dependent path reads one table probed once, so adding RT and mesh in 2.0 is a new row, not new probing code.

## Acceptance Criteria

- [ ] One capability table filled at device creation (subgroup size range and stages, descriptor indexing/buffer, ray query/pipeline, mesh shader, timestamp support, memory budget)
- [ ] Existing feature checks read it
- [ ] Unit test with a fake physical device; lavapipe test prints the probed table
- [ ] gpu-driver.md Interfaces updated

## Out of Scope

Using the RT or mesh rows (2.0 M9).

## Summary of Changes

TBD
