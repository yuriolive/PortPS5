---
# portps5-w9xc
title: 'Test: exceptions thrown inside libc.prx terminate Windows test executables'
status: todo
type: task
priority: low
created_at: 2026-10-01T03:50:00Z
updated_at: 2026-10-01T18:33:27Z
parent: portps5-7dk3
---


## Context

On the hosted Windows job, a GoogleTest executable that catches an exception thrown inside `libc.prx` (for example `GuestAllocations::Mutation::Find` on an unregistered address, or `Unmap` rejecting a range) terminates with `0xc0000409`, with a typed handler and with `catch (...)`. The same exception is caught fine inside `libkernel.prx` (`sceKernelMunmap` returns EINVAL). Found while testing `GuestAllocations::Mutation::Unmap` in PR for `feat/port-guest-memory`; the affected tests now skip on Windows and the behaviour is covered through the exports.

## Higher Goal

Registry and other libc contract errors become testable from test executables on every host.

## Acceptance Criteria

- [ ] Root cause identified (test exe unwind registration, separate C++ runtime, or the repo's `.ehfram` unwinder)
- [ ] Either a code fix, or a documented rule in docs/TESTING.md for tests that need a thrown error
- [ ] If a code fix is chosen: the skipped tests in `tests/memory/GuestAllocationsUnmapTests.cpp` run on Windows. If the documented rule is chosen instead: the skips stay, and the rule names the export-level tests that cover the same behaviour.

## Out of Scope

Changing which errors the registry throws (the no-throw direction is tracked with the arena wiring, portps5-421p).

## Summary of Changes

TBD
