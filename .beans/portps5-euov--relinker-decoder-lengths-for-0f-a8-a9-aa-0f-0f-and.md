---
# portps5-euov
title: 'Relinker decoder: lengths for 0F A8, A9, AA, 0F 0F and 0F FF'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:50:40Z
updated_at: 2026-09-30T23:50:40Z
---

## Context

The x86-64 length decoder mis-sizes or rejects these opcodes; PR #64 notes they are not emitted by user-mode PS5 code, so it deferred them. Spec: docs/spec/relinker.md Open questions (decoder length gaps).

## Higher Goal

A CodeMap that never desynchronizes on any valid user-mode encoding.

## Acceptance Criteria

- [ ] Decide per opcode: fix the length or prove it cannot occur in user-mode guest code and record the proof
- [ ] GoogleTest on synthetic bytes for each fixed opcode

## Out of Scope

Privileged or AMD-only opcode handling beyond the existing --to-intel scope.

## Summary of Changes

TBD
