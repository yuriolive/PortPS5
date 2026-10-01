---
# portps5-k7qd
title: 'Kernel: virtual range names and a real sceKernelVirtualQuery'
status: todo
type: feature
priority: low
created_at: 2026-10-01T03:30:00Z
updated_at: 2026-10-01T17:59:27Z
parent: portps5-w3s8
---


## Context

AnyPS5 9754937a adds `sceKernelSetVirtualRangeName`/`ClearVirtualRangeName` and reports names through `sceKernelVirtualQuery`. Here `sceKernelVirtualQuery` is still a one-page stub (`libkernel/DirectMemory/Export.cpp`) and `sceKernelSetVirtualRangeName` is a NotImplemented stub, so names have nothing to attach to. The name pointer is a guest string and must go through `GuestMemoryValidation` (PR for bean portps5-8l0d). Reviewed and deferred in docs/spec/guest-memory.md "Upstream mapping commits".

## Higher Goal

Titles that label their mappings and query them back see consistent data.

## Acceptance Criteria

- [ ] `sceKernelVirtualQuery` answers from the registry (start, end, protection, direct/flexible) instead of one page
- [ ] Names stored in an interval map, split on partial overwrite or clear, copied with a bounded, validated read (max 31 bytes)
- [ ] GoogleTest for split, clear, overlap and an unreadable name pointer

## Out of Scope

Per-title name conventions; the batch-map exports.

## Summary of Changes

TBD
