---
# portps5-r2ns
title: 'M5: direct memory mapped at several addresses (shared views)'
status: todo
type: task
priority: low
created_at: 2026-10-01T03:30:00Z
updated_at: 2026-10-01T17:59:27Z
parent: portps5-w3s8
---


## Context

AnyPS5 abb9c852, b7eaabbc, 03465e76, f7c53a44 and 5e08eb4c share one physical direct-memory range between several guest addresses and keep write watching correct for each view. They are Windows placeholder/section code (`VirtualAlloc2`, `MapViewOfFile3`) on top of upstream `GuestArena`, which this tree does not have. Reviewed and deferred in docs/spec/guest-memory.md "Upstream mapping commits"; Open question 1 (does any gate title map one physical range twice?) is unanswered.

## Higher Goal

Aliased direct memory without breaking the GPU driver write tracking (ROADMAP M5).

## Acceptance Criteria

- [ ] M1 import inventory answers whether any gate title maps one physical range at two addresses
- [ ] If yes: design on top of the wired arena (portps5-421p), no throws, no `APS5_*` switch, placeholder splits batched instead of per 16 KiB page
- [ ] GoogleTest with two views of one page and the write tracker

## Out of Scope

Anything before the arena and tracker are wired.

## Summary of Changes

TBD
