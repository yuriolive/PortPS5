---
# portps5-51d6
title: 'Recompiler: FP64 literal constant as high dword in readU32Pair'
status: todo
type: bug
priority: normal
created_at: 2026-09-30T23:50:43Z
updated_at: 2026-09-30T23:50:43Z
---

## Context

On hardware a 64-bit floating-point operand with a literal constant takes the literal as the high dword of the double (low dword zero), while readU32Pair zero-extends it, which is correct for integer operands only. Needs the consuming opcode type. Spec: docs/spec/shader-recompiler.md Open question 8.

## Higher Goal

Correct f64 literal operands in SPIR-V output.

## Acceptance Criteria

- [ ] readU32Pair (or its callers) takes the operand type into account
- [ ] Synthetic golden and unit test with an f64 literal operand
- [ ] shader-recompiler.md Open question 8 updated

## Out of Scope

Other Lane E follow-ups (separate bean).

## Summary of Changes

TBD
