---
# portps5-4ut1
title: 'M1: GPU Driver & Vulkan Translation Layer (Direct submission, VK_EXT_pageable_device_local_memory, SceAgc/Gnm)'
status: todo
type: task
priority: high
tags:
    - beads:portps5-3
created_at: 2026-09-30T22:53:49Z
updated_at: 2026-09-30T23:56:41Z
---

## Context

Epic for the M1 driver and Vulkan translation layer (docs/spec/gpu-driver.md). Landed: Recorder and HostImport building blocks (PR #49), depth/stencil and conditional colour-write state decode (PR #50), texture detile. In flight, not landed: host depth/stencil surface (PR #55, portps5-mij8), Vulkan 1.3 floor (PR #62). Open: Recorder wiring (portps5-tiod), `driver-lavapipe` job (portps5-ekx3), module split and capture-ordering redesign (M3). Migrated from beads `portps5-3`.

## Higher Goal

Guest PM4 command buffers become Vulkan work with no silent skips, no title HLE and no environment switches.

## Acceptance Criteria

- [x] Adapted Recorder and HostImport landed (PR #49)
- [x] Depth/stencil state decode and pipeline cache keying (PR #50)
- [ ] Submit path recorded through Recorder and HostImport: portps5-tiod
- [ ] Host depth/stencil surface: portps5-mij8 (PR #55)
- [ ] Vulkan 1.3 floor: PR #62 (in flight)
- [ ] `driver-lavapipe` hosted job: portps5-ekx3

## Out of Scope

Indirect family, module split, capture-ordering redesign (M3), descriptor heap (M4), host-import budget (M5).

## Summary of Changes

Partial: PRs #49 and #50 so far. Verified on main 2026-09-30: no `matchesFillKernel`, `matchesCopyKernel` or skip-on-throw paths exist in the ported driver.
