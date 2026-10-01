---
# portps5-b2ya
title: 'libc: reallocalign of an existing block'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:52:15Z
updated_at: 2026-09-30T23:52:15Z
---

## Context

reallocalign of an existing block needs an allocator usable-size query, so it still aborts through Unsupported(). Spec: docs/spec/libc.md Milestones (open box).

## Higher Goal

Complete the mspace and heap front-end contract.

## Acceptance Criteria

- [ ] Usable-size query added to the allocator
- [ ] reallocalign of an existing block returns a block or ENOMEM, with tests for grow, shrink and overflow

## Out of Scope

Mspace thread-safety model changes.

## Summary of Changes

TBD
