---
# portps5-x5vh
title: 'Pipeline cache: canonicalise pipeline keys'
status: todo
type: task
created_at: 2026-10-01T21:28:54Z
updated_at: 2026-10-01T21:28:54Z
parent: portps5-oo21
blocked_by:
    - portps5-8gdr
---

## Context

KytyPS5 pipelineCache.cpp zeroes inactive blend and depth state before hashing and shares vertex shaders across equivalent vertex layouts; PortPS5 keys every field, so equivalent states compile twice.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Equivalent states share one pipeline in memory and in the disk cache.

## Acceptance Criteria

- [ ] Key normalisation: inactive blend and depth fields zeroed, equivalent vertex layouts merged
- [ ] Test: equivalent states give the same key; different states never collide

## Out of Scope

Dynamic state (separate bean).
