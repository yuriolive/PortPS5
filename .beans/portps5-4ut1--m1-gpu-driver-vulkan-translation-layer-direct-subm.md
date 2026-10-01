---
# portps5-4ut1
title: GPU driver and Vulkan translation (M1-M2)
status: todo
type: epic
priority: high
tags:
    - beads:portps5-3
created_at: 2026-09-30T22:53:49Z
updated_at: 2026-10-01T18:33:23Z
parent: portps5-7dk3
---

## Context

Epic for the GPU driver and Vulkan translation work of M1 and M2 (docs/spec/gpu-driver.md, ROADMAP M1 and M2 driver items). Migrated from beads `portps5-3`; refreshed against `main` on 2026-10-01.

Landed: Recorder and HostImport building blocks (PR #49), depth/stencil and conditional colour-write state decode (PR #50), texture detile, host depth/stencil surface (PR #55, portps5-mij8), Vulkan 1.3 driver floor (PR #62, portps5-pjy1), lavapipe test repairs (PR #80, portps5-jbdl), the `driver-lavapipe` hosted job (PR #82, portps5-ekx3), the stencil null-base fix (PR #79) and the resident render-target image pool (PR #74, portps5-r7qk step 1).

In flight, not landed: per-draw texture cost (PRs #71, #75, #77).

## Higher Goal

Guest PM4 command buffers become Vulkan work with no silent skips, no title HLE and no environment switches.

## Acceptance Criteria

- [x] Adapted Recorder and HostImport landed (PR #49)
- [x] Depth/stencil state decode and pipeline cache keying (PR #50)
- [x] Host depth/stencil surface: portps5-mij8 (PR #55)
- [x] Vulkan 1.3 floor: portps5-pjy1 (PR #62)
- [x] `driver-lavapipe` hosted job: portps5-ekx3 (PR #82)
- [ ] Submit path recorded through Recorder and HostImport: portps5-tiod
- [ ] Retile depth to guest memory and upload never-cleared depth: portps5-9s7e
- [ ] Interim FMV adjacent block-generation advance: portps5-ux18
- [ ] Resident render-target sampling measured and step 2 decided: portps5-r7qk

## Out of Scope

Indirect family, module split, capture-ordering redesign (M3), descriptor heap (M4), host-import budget (M5). Display config keys reaching the swapchain are in the runtime-wiring epic (portps5-r8mh, child portps5-dtwf).

## Summary of Changes

Partial; see the ticked items above. Verified on main 2026-09-30: no `matchesFillKernel`, `matchesCopyKernel` or skip-on-throw paths exist in the ported driver.
