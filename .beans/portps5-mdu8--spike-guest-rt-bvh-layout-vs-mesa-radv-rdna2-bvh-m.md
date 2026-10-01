---
# portps5-mdu8
title: 'Spike: guest RT BVH layout vs Mesa RADV RDNA2 BVH (M9 feasibility)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:12:43Z
updated_at: 2026-10-01T18:15:27Z
parent: portps5-epoi
---

## Context

2.0 gates 7-9 use hardware ray tracing (PRD V4, V-R3). The guest builds BVHs in the RDNA2 layout; Mesa RADV documents and implements that layout publicly (MIT). Blocked by: none; it can run at any time with locally owned dumps.

## Higher Goal

Know before M9 whether guest BVHs can be rebuilt as Vulkan acceleration structures or must be traversed in shader, and at what cost.

## Acceptance Criteria

- [ ] Summarise the RDNA2 BVH node layout from Mesa RADV (cite commit and path)
- [ ] Observe, on a local dump, how the guest builds and consumes BVHs (API calls and counts only; no game data committed)
- [ ] Options with cost estimates recorded in a new spec Open question (gpu-driver.md or a ray-tracing spec stub)

## Out of Scope

Implementation (2.0 M9).

## Summary of Changes

TBD
