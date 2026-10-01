---
# portps5-7s7x
title: Assess 67,605 unproven --to-intel bytes runtime risk (PPSA02929)
status: todo
type: task
priority: normal
tags:
    - beads:portps5-55
created_at: 2026-09-30T22:53:50Z
updated_at: 2026-09-30T23:56:55Z
---

## Context

The Dreaming Sarah (PPSA02929) conversion left 67,605 unproven bytes (7,601 ranges) unpatched with 0 residual sites, and the syscall scan ignored them. If the game executes any of them, behaviour depends on the runtime trap path. Still open: no boot has been recorded. Migrated from beads `portps5-55`. Spec: docs/spec/relinker.md Open question 3.

## Higher Goal

Know whether `--to-intel` leaves runtime risk, and record the answer so the conversion policy for unproven bytes is evidence-based.

## Acceptance Criteria

- [ ] Boot evidence that unproven bytes do or do not execute, or a trap-coverage test
- [ ] relinker.md Open question 3 updated with the outcome (counts only, no bytes from the dump)

## Out of Scope

Changing the unproven-bytes policy before evidence exists.

## Summary of Changes

TBD
