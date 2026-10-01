---
# portps5-8l0d
title: Guest-memory range-validation API for PRXs
status: todo
type: feature
priority: normal
created_at: 2026-09-30T23:51:45Z
updated_at: 2026-09-30T23:51:45Z
---

## Context

Library code that receives guest pointers (for example libSceJpegEnc and libScePngDec) cannot validate that a range lies in the guest arena because no such API is exported to PRXs yet. The page-state table and WriteWatchTracker (PR #40) provide the data. Specs: docs/spec/image-codecs.md Open questions, docs/spec/guest-memory.md.

## Higher Goal

All guest pointers are untrusted and validated through one guest-memory API (cpp-style rule).

## Acceptance Criteria

- [ ] Exported, noexcept range check returning a code for not-guest, uncommitted and protection mismatches
- [ ] Codec exports use it for input and output buffers
- [ ] GoogleTest on synthetic windows

## Out of Scope

Pinning and write tracking semantics.

## Summary of Changes

TBD
