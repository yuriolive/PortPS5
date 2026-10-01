---
# portps5-t5qa
title: 'Recompiler: native GVN/CSE, copy propagation and control-flow simplification passes'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:23:42Z
updated_at: 2026-10-01T18:32:57Z
parent: portps5-7fqk
---

## Context

Release SPIR-V gets only ConstantFold, DeadCodeEliminator and ReadLaneEliminator (shader-recompiler.md stage 6), since SPIRV-Tools is off in release. Blocked by: none.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) GPU time without depending on the R1 licence question.

## Acceptance Criteria

- [ ] GVN/CSE, copy propagation and control-flow simplification passes over the SSA IR
- [ ] Golden SPIR-V updated; spirv-val clean; fuzz corpus green
- [ ] Before/after shader instruction counts on the synthetic corpus

## Out of Scope

spirv-opt (portps5-m5n7).

## Summary of Changes

TBD
