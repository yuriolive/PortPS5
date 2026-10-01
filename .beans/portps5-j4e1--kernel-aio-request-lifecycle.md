---
# portps5-j4e1
title: 'Kernel: Aio request lifecycle'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:50:57Z
updated_at: 2026-10-01T18:14:35Z
parent: portps5-epoi
blocked_by:
    - portps5-37j0
---


## Context

sceKernelAio* (core/libs/prx/libkernel/Aio/src/Aio.cpp) lacks a real request lifecycle. PR #61 (merged) skipped the upstream commit because it needs a race-free positional fd primitive: NativePread emulates pread with seek/read/restore (racy), and upstream's _dup shares the file offset on Windows. Spec: docs/spec/threading.md Open questions.

## Higher Goal

Async file I/O that a title can poll and cancel without corrupting shared file offsets.

## Acceptance Criteria

- [ ] Positional read primitive that does not share the file offset
- [ ] Request ids never reuse a live id
- [ ] Return-code tests for submit, poll, wait, cancel and delete

## Out of Scope

Filesystem mount changes (PR #53).

## Summary of Changes

TBD
