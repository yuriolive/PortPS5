---
# portps5-vs66
title: 'libc: null replacement-table fallback for the application heap'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:52:02Z
updated_at: 2026-10-01T18:33:27Z
parent: portps5-7dk3
---


## Context

ApplicationHeap falls back to the guest heap only when every slot of the replacement table is empty; a null table pointer, or a partially filled table, is not handled as a fallback. Spec: docs/spec/libc.md Open questions.

## Higher Goal

Defined behaviour for every allocator replacement shape a title can pass.

## Acceptance Criteria

- [ ] Decide null-table behaviour and document it
- [ ] GoogleTest for null, empty and partial tables
- [ ] libc.md Open question closed

## Out of Scope

Changing the all-empty fallback.

## Summary of Changes

TBD
