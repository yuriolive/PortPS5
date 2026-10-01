---
# portps5-mij8
title: 'M2: Host depth/stencil surface (PR #55)'
status: completed
type: feature
priority: high
created_at: 2026-09-30T23:46:42Z
updated_at: 2026-10-01T00:56:55Z
---

## Context

PR #50 landed depth/stencil and conditional colour-write state decode, but the host has no depth image: a draw that enables a depth, stencil or depth-bounds test while DB_Z_INFO/DB_STENCIL_INFO binds a surface is rejected (Graphics/src/State.cpp:146-147 on main). PR #55 (merged 2026-09-30) adds the host depth/stencil VkImage, DB_Z_* decode, clears and depth bounds; retile to guest memory is deferred there. Spec: docs/spec/gpu-driver.md (Depth/stencil, open question 8).

## Higher Goal

Depth-tested 3D draws run against a real host depth image instead of being rejected, every unsupported combination is a logged error, no title-specific code (ROADMAP M2).

## Acceptance Criteria

- [x] PR #55 reviewed, rebased on main and merged
- [x] Bound-surface rejection removed, with hosted GoogleTest coverage
- [x] gpu-driver.md Current state and M2 row updated; ROADMAP M2 driver item ticked
- [x] Follow-up bean filed for depth retile and never-cleared surface upload (portps5-9s7e)

## Out of Scope

Retile of depth to guest memory, partial clears, MSAA/array/mip depth, HTILE-aware paths (deferred in PR #55).

## Summary of Changes

PR #55: `Graphics/src/{State,DepthFormat,DepthSurface,Pipeline,GraphicsPipelineCache,RenderCache,Draw}.cpp`, `Execution/src/VulkanDevice.cpp` (depthBounds feature), tests `DepthStencilState.cpp` and `DepthSurface.cpp`, `docs/spec/gpu-driver.md`, `docs/ROADMAP.md`. Depth retile and never-cleared upload are tracked in portps5-9s7e.
