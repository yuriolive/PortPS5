---
# portps5-gkef
title: 'Guest memory registry: physical allocation with N views (1 view wired)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:12:42Z
updated_at: 2026-10-01T18:15:26Z
parent: portps5-epoi
blocking:
    - portps5-r2ns
blocked_by:
    - portps5-421p
---

## Context

M5 needs one physical range mapped at several guest addresses (portps5-r2ns), and 2.0 streaming needs it at full size. If the registry built by portps5-421p maps one address to one allocation, M5 redoes it. Blocked by: portps5-421p (the registry must be wired first).

## Higher Goal

The guest memory registry's data model is physical allocation -> N views from day one, so aliasing is an implementation step, not a redesign.

## Acceptance Criteria

- [ ] Registry stores physical allocations and their views; every current path creates exactly one view
- [ ] Write tracker and page-state table key on the physical allocation
- [ ] GoogleTest: synthetic two-view case exercises the data model (views share generation and pins) without host aliasing
- [ ] guest-memory.md Target design updated

## Out of Scope

Host-level aliased mappings (portps5-r2ns, M5).

## Summary of Changes

TBD
