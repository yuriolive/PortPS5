---
# portps5-sian
title: 'Relinker: ElfReader wrap-free checks (PR #64)'
status: completed
type: bug
priority: high
created_at: 2026-09-30T23:50:29Z
updated_at: 2026-10-01T00:20:58Z
---


## Context

PR #56 (merged) fixed wraps in ElfReader::_readU16/32/64At and the Io readers, but its last commit 0654dd72 was pushed after the merge and is not on main. On main, ElfReader::ReadDynamicTags (ElfReader.cpp:170 Offset + FileSize), ReadSection (:212) and ReadSegment (:222) still sum untrusted values, so a huge size passes the bounds check and builds a vector from an inverted iterator range. PR #64 (merged 2026-09-30 as 064fc231) contains the fix and regression tests. Spec: docs/spec/relinker.md Failure modes.

## Higher Goal

A malformed dump fails with a clean RelinkerException, never undefined behaviour.

## Acceptance Criteria

- [x] PR #64 merged (2026-09-30)
- [x] ElfReaderBoundsTests.SegmentAndSectionSizeWrapRejected and DynamicTagsHugeFileSizeClamped fail without the fix and pass with it (mutation check in the PR)
- [x] relinker.md failure-mode row describes the merged behaviour, including the deliberately lenient ReadDynamicTags clamp

## Out of Scope

TranslateVirtualAddress (fails safe), other sites (see the follow-up bean).

## Summary of Changes

PR #64: `core/relinker/relinker/src/parsing/ElfReader.cpp` (`ReadSegment`, `ReadSection` use `offset > size || len > size - offset`; `ReadDynamicTags` clamps to the file and never sums untrusted values), `core/relinker/relinker/tests/ElfReaderBoundsTests.cpp`, `docs/spec/relinker.md`. Same-shape sites outside ElfReader are tracked in portps5-ld5w.
