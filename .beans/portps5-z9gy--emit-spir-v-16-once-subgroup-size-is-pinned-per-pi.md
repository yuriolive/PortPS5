---
# portps5-z9gy
title: Emit SPIR-V 1.6 once subgroup size is pinned per pipeline
status: todo
type: task
priority: normal
created_at: 2026-10-01T01:19:57Z
updated_at: 2026-10-01T01:19:57Z
---

## Context

SPIR-V 1.6 modules let the Vulkan driver choose the subgroup size per dispatch (implicit `ALLOW_VARYING_SUBGROUP_SIZE`). The recompiler bakes `target.subgroupSize` into its lane math (wave64 masks, DS swizzles, ballot/readlane lowering), and no pipeline path pins the size. PR #62 therefore raised the Vulkan floor to 1.3 but keeps emitting SPIR-V 1.3 (1.4 for mesh). See `docs/spec/gpu-driver.md` open question 9.

## Higher Goal

Emit one SPIR-V version (1.6) for every stage and use the core Vulkan 1.3 subgroup size control, without wrong-lane reads on devices that run a different subgroup size.

## Acceptance Criteria

- [ ] `subgroupSizeControl` and `computeFullSubgroups` features are enabled at device creation, and `target.subgroupSize` is checked against the device's min/max range.
- [ ] Every pipeline path (compute, graphics, mesh, tessellation, detile, colour transfer) chains `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` set to `target.subgroupSize`; compute also sets `REQUIRE_FULL_SUBGROUPS`.
- [ ] `VulkanDevice::Target()` returns SPIR-V 1.6.
- [ ] Golden and driver tests use SPIR-V 1.6 fixtures; GPU/lavapipe run recorded.

## Out of Scope

- Vulkan 1.4 features.
- Wave64 emulation changes beyond pinning the size (M4).

## Summary of Changes

To be filled in when the work lands.
