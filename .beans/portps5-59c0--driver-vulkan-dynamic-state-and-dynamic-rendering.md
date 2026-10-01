---
# portps5-59c0
title: 'Driver: Vulkan dynamic state and dynamic rendering'
status: todo
type: feature
created_at: 2026-10-01T21:28:54Z
updated_at: 2026-10-01T21:28:54Z
parent: portps5-oo21
blocked_by:
    - portps5-hkwd
---

## Context

No `VK_DYNAMIC_STATE_*` use in libSceAgcDriver on PortPS5 main@5dd65fe3: viewport, scissor, depth/stencil state and the render pass are baked into each pipeline, multiplying pipelines and compile stutter. shadPS4 vk_graphics_pipeline.cpp uses about 23 dynamic states.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Fewer pipelines and less stutter for the same draws (PRD invariants P2 and P3).

## Acceptance Criteria

- [ ] Dynamic viewport, scissor, depth bias, stencil reference and blend constants at least
- [ ] Dynamic rendering (Vulkan 1.3 core) replaces baked render passes
- [ ] Pipeline key drops the now-dynamic fields
- [ ] lavapipe tests; pipeline-creation count before and after on a perf scene

## Out of Scope

VK_EXT_extended_dynamic_state3.
