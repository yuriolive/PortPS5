---
# portps5-s16v
title: Shader validator caps SPIR-V at 1.4 but rect-list shaders can target 1.6
status: todo
type: task
priority: low
created_at: 2026-10-01T16:40:00Z
updated_at: 2026-10-01T18:33:28Z
parent: portps5-rd6g
blocked_by:
    - portps5-z9gy
---


## Context

`ShaderValidation.cpp:151` rejects SPIR-V above 1.4 (`words[1] <= 0x10400u`), while `RectListShaders.cpp:377` accepts targets up to 1.6 and `SpirvOptimizer.cpp` allows 1.6 for Vulkan 1.3/1.4. The rect-list test in `Graphics.cpp` looped over 1.3, 1.4 and 1.6 on `main`; the 1.6 pass fails with "unsupported SPIR-V version: 67072". `driver_lavapipe` exposed it.

## Higher Goal

One agreed SPIR-V ceiling across the recompiler, optimizer and driver validator.

## Acceptance Criteria

- [ ] Decide the ceiling (raise the validator to 1.6 for Vulkan 1.3+, or cap RectListShaders at 1.4)
- [ ] Rect-list test covers the chosen maximum again
- [ ] `docs/spec/gpu-driver.md` states the ceiling

## Out of Scope

Changing which SPIR-V version `VulkanDevice` targets today.

## Summary of Changes

TBD
