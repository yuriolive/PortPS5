---
# portps5-mn0m
title: 'Kernel: fix SCE error constants in DirectMemory.hpp'
status: todo
type: task
priority: normal
created_at: 2026-09-30T23:51:15Z
updated_at: 2026-10-01T00:21:11Z
---

## Context

`core/libs/prx/libkernel/DirectMemory/DirectMemory.hpp:18-22` defines its own `SCE_KERNEL_ERROR_*` statics with wrong `0x8001xxxx` values (`EINVAL` is -2147418107, which is `0x80010005`, not `0x80020016`), and the GoogleTest suites in `tests/memory/` pin those values. PR #61 added the shared `libkernel/KernelErrors.hpp` (`0x80020000 | errno`) and left this header alone because it belongs to the guest-memory owner; a header that declares its own statics cannot be included next to `KernelErrors.hpp`. Spec: docs/spec/threading.md Open question 6.

## Higher Goal

One shared, correct set of SCE kernel error codes, so titles that compare against the real constants see them.

## Acceptance Criteria

- [ ] `DirectMemory.hpp` and `MemoryPool` use `KernelErrors.hpp` and the local statics are removed
- [ ] `tests/memory/*` expectations updated together with the header, with the correct `0x80020000 | errno` values
- [ ] Any exported call that returned the wrong code is covered by a GoogleTest that fails on the old value
- [ ] threading.md Open question 6 closed

## Out of Scope

Behaviour changes to any error path other than the numeric value.

## Summary of Changes

TBD
