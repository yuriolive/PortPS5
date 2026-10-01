---
# portps5-7zu6
title: 'PM4: predication and conditional execution'
status: todo
type: feature
created_at: 2026-10-01T21:28:55Z
updated_at: 2026-10-01T21:28:55Z
parent: portps5-oo21
blocked_by:
    - portps5-hkwd
---

## Context

Pm4.cpp:106-107 on PortPS5 main@5dd65fe3 rejects SET_PREDICATION (0x20) and COND_EXEC (0x22). KytyPS5 graphicsRun.cpp implements both.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Command buffers that use predication or conditional execution run.

## Acceptance Criteria

- [ ] COND_EXEC skips the following dwords by the guest condition
- [ ] Predication through VK_EXT_conditional_rendering where available, otherwise evaluated at submit
- [ ] Unit tests on synthetic PM4 streams

## Out of Scope

Occlusion-query predication beyond what the extension offers.
