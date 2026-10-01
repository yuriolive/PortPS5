---
# portps5-65h0
title: 'Kernel: sceKernelLseek 64-bit offsets'
status: todo
type: bug
priority: normal
created_at: 2026-09-30T23:51:00Z
updated_at: 2026-09-30T23:51:00Z
---

## Context

sceKernelLseek is declared to return int (core/libs/prx/libkernel/File/src/Open.cpp:135). A result above INT_MAX throws std::overflow_error (Open.cpp:143-144), a failed lseek and an invalid whence also throw, and the POSIX wrapper (File/src/Stdio.cpp:165) only widens a successful int. So seeks past 2 GiB fail instead of returning the offset, and host exceptions cross the ABI. Spec: docs/spec/save-data.md (file I/O).

## Higher Goal

Correct seek results for large files, with SCE error codes for failures.

## Acceptance Criteria

- [ ] Return type widened to std::int64_t for the exports the guest calls as off_t
- [ ] GoogleTest at offsets above 2 GiB and for negative error paths

## Out of Scope

Mount table or sandbox changes.

## Summary of Changes

TBD
