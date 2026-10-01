---
# portps5-hps3
title: 'Driver caches: residency interface (size, last use, eviction hook)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:12:42Z
updated_at: 2026-10-01T18:15:27Z
parent: portps5-epoi
blocked_by:
    - portps5-hkwd
---

## Context

2.0 titles exceed host VRAM (PS5 unified memory vs a discrete GPU). Without a residency interface on the driver caches, M8 rewrites them. Blocked by: portps5-hkwd (the caches exist as modules after the split).

## Higher Goal

The 2.0 residency manager (M8) plugs into an interface every cache already implements.

## Acceptance Criteria

- [ ] Buffer and Texture caches report size, last-use frame and an eviction hook per resource
- [ ] Simple LRU, no budget pressure in 1.0
- [ ] GoogleTest on the LRU bookkeeping
- [ ] gpu-driver.md Target design updated

## Out of Scope

Budgeted eviction under VK_EXT_memory_budget (2.0 M8).

## Summary of Changes

TBD
