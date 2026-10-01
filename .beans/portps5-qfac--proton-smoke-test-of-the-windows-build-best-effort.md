---
# portps5-qfac
title: Proton smoke test of the Windows build (best effort)
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:12:41Z
updated_at: 2026-10-01T18:15:27Z
parent: portps5-epoi
---

## Context

Linux players could run the Windows build under Proton/Wine today, but nobody has tried, and WaitOnAddress, MEM_WRITE_WATCH and placeholder views are the likely gaps (host-platform.md open question 3). Blocked by: none.

## Higher Goal

Early signal on Linux behaviour before the native backend, at near-zero cost.

## Acceptance Criteria

- [ ] Run the Windows ctest binaries under a pinned Proton or Wine version locally or on a schedule; record pass/fail per suite
- [ ] Document the result in host-platform.md open question 3
- [ ] Never a PR gate

## Out of Scope

Native Linux, release support for Proton.

## Summary of Changes

TBD
