---
# portps5-7e2a
title: Shader printf at a guest shader address
status: todo
type: feature
created_at: 2026-10-01T21:35:20Z
updated_at: 2026-10-01T21:35:20Z
parent: portps5-etxc
blocked_by:
    - portps5-c5if
---

## Context

Bisecting a wrong shader result now means `debug.recompiler.single_lane` and IR dumps. Vulkan's NonSemantic.DebugPrintf (VK_KHR_shader_non_semantic_info, printed by the validation layer) can print chosen guest registers at a chosen guest PC on real hardware.

Blocked by: portps5-c5if.

## Higher Goal

A wrong value is traced to its RDNA instruction in one run.

## Acceptance Criteria

- [ ] `debug.recompiler.printf = [{shader, pc, regs}]` injects DebugPrintf after the instruction at `pc`
- [ ] Instrumented modules never enter the disk pipeline cache (the EnvKey includes the printf set)
- [ ] Golden test on a synthetic shader checks the injected SPIR-V

## Out of Scope

A source-level shader debugger.
