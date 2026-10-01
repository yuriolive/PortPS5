---
# portps5-cfbs
title: 'Port AnyPS5 #198: honour MAP_FIXED in sceKernelReserveVirtualRange and MemoryPoolReserve'
status: todo
type: bug
priority: normal
created_at: 2026-10-01T20:53:14Z
updated_at: 2026-10-01T20:53:14Z
parent: portps5-w3s8
---

## Context

PortPS5 `DirectMemory/Export.cpp:144` documents that `flags are ignored`, so a fixed-address reservation lands elsewhere. AnyPS5 e4eaba6 (#198) fixes it. Upstream source: AnyPS5 main@709d7fe (pulled 2026-10-01); cite the source commit in the port's commit body (.agents/rules/git-workflow.md). Blocked by: none.

## Higher Goal

Reservation placement matches the guest's request or returns the console error.

## Acceptance Criteria

- [ ] MAP_FIXED honoured; occupied range returns the documented SCE error
- [ ] GoogleTest for fixed, hinted and occupied cases
- [ ] guest-memory.md updated

## Out of Scope

Aliasing (portps5-r2ns).

## Summary of Changes

TBD
