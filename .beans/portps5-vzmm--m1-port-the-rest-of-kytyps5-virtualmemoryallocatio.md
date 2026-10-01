---
# portps5-vzmm
title: 'M1: Port the rest of KytyPS5 VirtualMemoryAllocationTests'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:54:12Z
updated_at: 2026-09-30T23:54:12Z
---

## Context

tests/memory/VirtualMemoryAllocationTests.cpp ports 241 of Kyty's 3,173 lines. The remaining cases cover 16 KiB rounding, direct-memory mapping and protect transitions. Spec: docs/spec/guest-memory.md Tests; ROADMAP M1 virtual memory item.

## Higher Goal

Regression coverage for the memory contracts the driver depends on.

## Acceptance Criteria

- [ ] Remaining cases re-expressed as GoogleTest or dropped with a written reason
- [ ] ROADMAP item ticked only for what is ported

## Out of Scope

IWriteTracker aliased-view tests (blocked on alias support).

## Summary of Changes

TBD
