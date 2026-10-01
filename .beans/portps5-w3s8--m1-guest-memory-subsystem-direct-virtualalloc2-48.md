---
# portps5-w3s8
title: 'M1: Guest Memory Subsystem (Direct VirtualAlloc2 48-bit VA, Write Tracker, SceKernelMemory)'
status: todo
type: task
priority: high
tags:
    - beads:portps5-1
created_at: 2026-09-30T22:53:48Z
updated_at: 2026-09-30T23:56:40Z
---

## Context

Epic for the M1 guest memory subsystem (docs/spec/guest-memory.md). Landed: extent allocator (PR #32), direct memory block tracking and memory pool exports (PR #39), `WriteWatchTracker`, page-state table and pins (PR #40), memory-tracking tests (PR #52). Not landed: wiring of those pieces into the arena, heap and registry (portps5-421p), the rest of the Kyty virtual-memory test port (portps5-vzmm). Migrated from beads `portps5-1`.

## Higher Goal

Guest memory placement, protection and tracking that the GPU driver and FMV depend on, with no throws across the ABI.

## Acceptance Criteria

- [x] Direct memory block tracking and pool exports with SCE error codes (PR #39)
- [x] Extent-tree allocator core (PR #32)
- [x] Real write tracker, page-state table, explicit pins (PR #40)
- [x] Memory-tracking behaviour tests on `GuestMemoryTracking::Watch` (PR #52)
- [ ] Arena, heap and registry wiring: portps5-421p
- [ ] Remaining `VirtualMemoryAllocationTests` port: portps5-vzmm
- [ ] Range-validation API for PRXs: portps5-8l0d

## Out of Scope

Section-window aliasing (M5), `MarkWritten` driver integration (M3).

## Summary of Changes

See the child items. Specs: docs/spec/guest-memory.md.
