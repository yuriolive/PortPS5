---
# portps5-gr4c
title: 'agc: snapshot comparisons race with guest writes (pre-existing)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T03:30:00Z
updated_at: 2026-10-01T03:30:00Z
parent: portps5-7fqk
blocked_by:
    - portps5-421p
---

## Context

Blocked by: `portps5-421p` (the runtime write tracker decides the contract). `TextureCache::Get` and `GuestBufferMemory::AddSnapshot` compare live guest memory with a snapshot while guest threads may write that memory. This is a data race and was already true with the `memcmp` calls that `BytesEqual` replaced (found in review of the `portps5-pdc1` slice 1 PR). A multi-block compare can also observe different moments, so "equal" does not prove the range was ever equal at one instant, and a missed write is only caught by a later revalidation.

## Higher Goal

A coherent snapshot contract for guest memory read by the GPU driver, so equality checks and uploads are correct by construction. The write tracker (`IWriteTracker`, bean `portps5-421p`) is the planned mechanism; `portps5-pdc1` slice 2 uses it to skip compares.

## Acceptance Criteria

- [ ] Decide the contract: which reads of guest memory by the driver must be coherent, and what excludes or detects concurrent writers (tracker generations, pins, or a documented tolerated race).
- [ ] Apply it to the TextureCache revalidation and the `GuestBufferMemory` snapshot checks, with a regression test that does not need game data.
- [ ] Update `docs/spec/gpu-driver.md` and `docs/spec/guest-memory.md`.

## Out of Scope

- Performance work (tracked by `portps5-pdc1`).
- Creating the runtime tracker and `MEM_WRITE_WATCH` arena (`portps5-421p`).

## Summary of Changes

None yet. Opened to track the finding instead of leaving it as a code comment only.
