---
# portps5-9gsx
title: 'Recompiler: specialization constants for draw state keyed in VariantKey'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:23:43Z
updated_at: 2026-10-01T18:33:01Z
parent: portps5-7fqk
blocked_by:
    - portps5-8gdr
---

## Context

Draw state (formats, sample count, fixed descriptor layout) is known at pipeline creation but shaders are generic. Blocked by: portps5-8gdr (VariantKey in the disk cache).

## Higher Goal

Performance track (ROADMAP, PRD 4.5) GPU time: specialized variants without unbounded variant growth.

## Acceptance Criteria

- [ ] Specialization constants for selected draw-state fields, keyed in VariantKey
- [ ] Variant count bounded and logged; cache stays stable
- [ ] Golden and lavapipe tests per specialized field

## Out of Scope

New pipeline-cache format.

## Summary of Changes

TBD
