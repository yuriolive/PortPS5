---
# portps5-421p
title: 'M1: Adapted GuestArena extent-tree port'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-7
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-09-30T23:56:39Z
---

## Context

The raw upstream `GuestArena` was trial-ported on PR #28 and reverted (O(n) scan, throws, unwired). Since then the building blocks landed: PR #32 `ExtentAllocator` (`libc/src/GuestArenaExtent.cpp`, treap with subtree-max, 10^6-op differential test against the reference linear first-fit), PR #39 direct memory block tracking with SCE error codes, PR #40 `WriteWatchTracker`, `PageStateTable` and explicit pin tokens. None is instantiated by the runtime yet: on main `ExtentAllocator` and `WriteWatchTracker` are referenced only from tests. Migrated from beads `portps5-7`. Spec: docs/spec/guest-memory.md.

## Higher Goal

One arena with O(log n) first-fit, span-level heap registration, explicit pins and a tracker the driver can use (ROADMAP M1 guest memory, PRD F4 FMV dependencies).

## Acceptance Criteria

- [x] Extent-tree allocator core with differential tests (PR #32)
- [x] `WriteWatchTracker`, registry-owned page-state table, explicit pins (PR #40)
- [x] Direct-memory return codes instead of throws (PR #39)
- [ ] Arena reservation (write-watched, ascending first-fit) uses `ExtentAllocator`
- [ ] `GuestHeap` registers 1 MiB spans and large blocks only; small frees never touch the registry
- [ ] Registry becomes an interval map under an SRWLOCK with generations bumped only by map, unmap, protect and decommit
- [ ] Runtime instantiates `WriteWatchTracker` and the driver registers the flush hook
- [ ] `IWriteTracker` aliased-view tests once alias support exists
- [ ] Spec M1 box and ROADMAP items ticked

## Out of Scope

Direct-memory aliasing through section windows (M5), `MarkWritten` consumers in the driver (M3).

## Summary of Changes

Partial, see the ticked items: PRs #32, #39, #40. Files: `core/libs/prx/libc/include/{GuestArenaExtent,WriteTracker}.hpp`, `src/{GuestArenaExtent,WriteTracker}.cpp`, `tests/memory/GuestArenaExtentTests.cpp`, `core/libs/tests/WriteTracker.cpp`.
