---
# portps5-8l0d
title: Guest-memory range-validation API for PRXs
status: completed
type: feature
priority: normal
created_at: 2026-09-30T23:51:45Z
updated_at: 2026-10-01T02:00:00Z
---

## Context

Library code that receives guest pointers (for example libSceJpegEnc and libScePngDec) cannot validate that a range lies in the guest arena because no such API is exported to PRXs yet. The page-state table and WriteWatchTracker (PR #40) provide the data. Specs: docs/spec/image-codecs.md Open questions, docs/spec/guest-memory.md.

## Higher Goal

All guest pointers are untrusted and validated through one guest-memory API (cpp-style rule).

## Acceptance Criteria

- [x] Exported, noexcept range check returning a code for not-guest, uncommitted and protection mismatches
- [x] Codec exports use it for input and output buffers
- [x] GoogleTest on synthetic windows

## Out of Scope

Pinning and write tracking semantics.

## Summary of Changes

- `libc/include/GuestMemoryValidation.hpp`, `libc/src/GuestMemoryValidation.cpp`: `CheckReadable`/`CheckWritable`, overflow-safe, noexcept; registry-authoritative with a host-mapping fallback for unregistered memory (stacks, TLS).
- `GuestAllocationsCover`: noexcept registry walker with a moving cursor for ranges spanning several entries.
- `libSceJpegEnc`, `libScePngDec`: validate every param struct, handle, work memory, pixel and output buffer.
- Tests: `tests/memory/GuestMemoryValidationTests.cpp` (8 cases), `JpegEncGuestRanges`/`PngDecGuestRanges` in `core/libs/tests`.
- Specs: `docs/spec/guest-memory.md` "Validation API", `docs/spec/image-codecs.md`.
- Page-state table was not used as the data source: it covers only tracked arena pages, while the PRX buffers can be stack or module memory.
