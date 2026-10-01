---
# portps5-8x1k
title: 'Build: x86-64-v3 with -ffp-contract=off and LTO for prx libraries'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:25:11Z
updated_at: 2026-10-01T18:33:00Z
parent: portps5-7fqk
---

## Context

The build sets no -march, LTO or PGO. Guest code already needs AVX2, so x86-64-v3 costs no compatibility. Blocked by: none.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) free host CPU gain.

## Acceptance Criteria

- [ ] release preset builds runtime libraries with -march=x86-64-v3 and -ffp-contract=off (no FMA contraction changing float results)
- [ ] LTO for prx libraries where the DWARF unwinder and exports still validate
- [ ] Before/after on microbenchmarks (portps5-i58o) and one perf scene
- [ ] build-toolchain.md updated

## Out of Scope

PGO (portps5-f2r1).

## Summary of Changes

TBD
