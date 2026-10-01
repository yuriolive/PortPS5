---
# portps5-afme
title: 'M2: Enable libSceMouse exports (undisable GTest)'
status: todo
type: task
priority: low
tags:
    - beads:portps5-10
created_at: 2026-09-30T22:53:48Z
updated_at: 2026-10-01T18:33:21Z
parent: portps5-dbpx
---


## Context

The mouse backend (`libSceMouse/src/mouse_impl.cpp`) and VideoOut routing exist, and `libSceMouse/tests/Mouse.cpp` builds but is DISABLED. The guest-visible `sceMouse*` exports in `libSceMouse/Export.cpp` are `Unsupported()` stubs; the file's own comment records that the M1 inventory shows no gate title importing libSceMouse at boot, so priority is low until an inventory says otherwise. Migrated from beads `portps5-10`. Spec: docs/spec/input.md.

## Higher Goal

A direct libSceMouse import is either implemented or a deliberate loud gap, as the M2 input matrix decides.

## Acceptance Criteria

- [ ] `sceMouseInit`, `Open`, `Read` and `Close` implemented over the backend (or the gap confirmed after the remaining inventories, portps5-3eh1)
- [ ] `DISABLED_` dropped from the mouse GoogleTest and green in CI
- [ ] Process-global `mouseMutex` (`mouse_impl.cpp`) removed
- [ ] input.md M2 matrix records mouse

## Out of Scope

Keyboard exports (`libSceKeyboard`), mouse-look changes.

## Summary of Changes

TBD
