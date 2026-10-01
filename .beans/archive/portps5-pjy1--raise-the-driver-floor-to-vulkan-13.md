---
# portps5-pjy1
title: Raise the driver floor to Vulkan 1.3
status: completed
type: task
priority: high
created_at: 2026-10-01T01:19:57Z
updated_at: 2026-10-01T01:19:57Z
---

## Context

The AGC driver created a Vulkan 1.1 instance and required Vulkan 1.1 devices, while the README and PRD §4.4 already require a Vulkan 1.3 GPU. Code lagged the documented reference tier.

## Higher Goal

Align the driver with the documented hardware floor and make Vulkan 1.3 core features (subgroup size control, `maintenance4`, dynamic rendering, `synchronization2`) available to later work. Vulkan 1.4 stays an optional, capability-gated optimization.

## Acceptance Criteria

- [x] Driver requests a Vulkan 1.3 instance and skips devices reporting below 1.3.
- [x] `VulkanDevice::Target()` returns `{VK_API_VERSION_1_3, SPIR-V 1.3}` (1.4 for mesh), with a comment explaining why 1.6 is deferred.
- [x] Rect-list tessellation builder accepts SPIR-V up to 1.6.
- [x] Test fixtures mirror `VulkanDevice::Target()`; test Vulkan instances and `BdaDevice` device selection use 1.3.
- [x] GTest `SpirvTargetVersionTests` pins the optimizer pairing (1.3 and 1.4 accept a 1.6 target; 1.1 and 1.2 reject it).
- [x] `docs/spec/gpu-driver.md` and `docs/spec/shader-recompiler.md` updated, including open question 9.

## Out of Scope

- Emitting SPIR-V 1.6 (tracked in its own bean).
- Requiring or using Vulkan 1.4 features.
- The pre-existing AGC test failures (`agc_driver_graphics`, `agc_driver_bda_device`, `agc_driver_pm4`, `agc_driver_recompiler`) and the `github-advanced-security` model failure.

## Summary of Changes

- `VulkanDevice.cpp`: apiVersion 1.3, device selection at least 1.3, `Target()` keeps SPIR-V 1.3/1.4.
- `RectListShaders.cpp`: allow SPIR-V 1.3 to 1.6.
- Tests and tools: Vulkan 1.3 environment in `SyntheticCorpus.hpp`, `ShaderMemory.cpp`, `BdaContracts.cpp`, `BdaDevice.cpp`, `RecorderTestSupport.hpp`, `Graphics.cpp`, `AgcShaderReplay.cpp`; new `SpirvTargetVersionTests`.
- Specs: gpu-driver (Decision, Failure modes, Open question 9), shader-recompiler (Driver target).
- PR: https://github.com/yuriolive/PortPS5/pull/62
