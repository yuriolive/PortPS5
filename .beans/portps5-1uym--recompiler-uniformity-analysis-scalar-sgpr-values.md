---
# portps5-1uym
title: 'Recompiler: uniformity analysis (scalar SGPR values as subgroup-uniform)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:23:42Z
updated_at: 2026-10-01T18:33:00Z
parent: portps5-7fqk
---

## Context

RDNA2 separates scalar (SGPR) and vector (VGPR) values; the recompiler emits all values per-lane, so drivers can't always prove uniformity. Blocked by: none.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) GPU time: uniform values marked subgroup-uniform let host compilers scalarize.

## Acceptance Criteria

- [ ] Uniformity analysis over the SSA IR (SGPR-derived values, readfirstlane results)
- [ ] Emitted SPIR-V uses uniform/scalar forms where provably uniform
- [ ] Golden tests on synthetic shaders; spirv-val clean

## Out of Scope

Wave64 subgroup pinning (portps5-cexw).

## Summary of Changes

TBD
