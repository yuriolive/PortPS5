---
# portps5-mij8
title: 'M2: Host depth/stencil surface (PR #55)'
status: todo
type: feature
priority: high
created_at: 2026-09-30T23:46:42Z
updated_at: 2026-09-30T23:46:42Z
---

## Context

PR #50 landed depth/stencil and conditional colour-write state decode, but the host has no depth image: a draw that enables a depth, stencil or depth-bounds test while DB_Z_INFO/DB_STENCIL_INFO binds a surface is rejected (Graphics/src/State.cpp:146-147 on main). PR #55 (open, not landed) adds the host depth/stencil VkImage, DB_Z_* decode, clears and depth bounds; retile to guest memory is deferred there. Spec: docs/spec/gpu-driver.md (Depth/stencil, open question 8).

## Higher Goal

Depth-tested 3D draws run against a real host depth image instead of being rejected, every unsupported combination is a logged error, no title-specific code (ROADMAP M2).

## Acceptance Criteria

- [ ] PR #55 reviewed, rebased on main and merged
- [ ] Bound-surface rejection removed, with hosted GoogleTest coverage
- [ ] gpu-driver.md Current state and M2 row updated; ROADMAP M2 driver item ticked
- [ ] Follow-up bean filed for depth retile and never-cleared surface upload

## Out of Scope

Retile of depth to guest memory, partial clears, MSAA/array/mip depth, HTILE-aware paths (deferred in PR #55).

## Summary of Changes

TBD
