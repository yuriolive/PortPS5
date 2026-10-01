---
# portps5-w3s8
title: Guest memory subsystem (M1 wiring, M5 aliasing)
status: todo
type: epic
priority: high
tags:
    - beads:portps5-1
created_at: 2026-09-30T22:53:48Z
updated_at: 2026-10-01T18:00:19Z
---

## Context

Epic for the guest memory subsystem (docs/spec/guest-memory.md, ROADMAP M1 "Guest memory" and "Virtual memory" items, M5 aliasing). Migrated from beads `portps5-1`; refreshed against `main` on 2026-10-01.

Landed: extent allocator core (PR #32), direct memory block tracking and memory pool exports with return codes (PR #39), `WriteWatchTracker` with page-state table and pins (PR #40), memory-tracking tests (PR #52), guest-pointer range validation for PRXs (PR #83, portps5-8l0d), direct-memory query, mapping placement and unmap exports with shared SCE error codes (PR #84, portps5-mn0m).

Not landed: wiring of the allocator and tracker into the arena, heap and registry (portps5-421p), the rest of the Kyty virtual-memory test port (portps5-vzmm).

## Higher Goal

Guest memory placement, protection and tracking that the GPU driver and FMV depend on, with no throws across the ABI.

## Acceptance Criteria

- [x] Direct memory block tracking and pool exports with SCE error codes (PR #39)
- [x] Extent-tree allocator core (PR #32)
- [x] Real write tracker, page-state table, explicit pins (PR #40)
- [x] Memory-tracking behaviour tests on `GuestMemoryTracking::Watch` (PR #52)
- [x] Range-validation API for PRXs: portps5-8l0d (PR #83)
- [x] Direct-memory query, mapping placement and unmap exports (PR #84)
- [ ] Arena, heap and registry wiring; the runtime instantiates the tracker: portps5-421p
- [ ] Remaining `VirtualMemoryAllocationTests` port: portps5-vzmm

## Out of Scope

Children that are not M1 exit work: shared direct-memory views (M5, portps5-r2ns), virtual range names and `sceKernelVirtualQuery` (portps5-k7qd), a Linux userfaultfd backend evaluation (portps5-u5fe). `MarkWritten` driver integration (M3).

## Summary of Changes

Partial; see the ticked items above. Specs: docs/spec/guest-memory.md.
