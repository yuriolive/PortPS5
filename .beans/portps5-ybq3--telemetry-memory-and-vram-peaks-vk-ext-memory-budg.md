---
# portps5-ybq3
title: 'Telemetry: memory and VRAM peaks (VK_EXT_memory_budget, guest commit, host import)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:31:56Z
updated_at: 2026-10-01T18:31:56Z
parent: portps5-7fqk
blocked_by:
    - portps5-w1re
    - portps5-l77s
---

## Context

No telemetry reports memory; 2.0 residency (M8) and the M5 host-import budget need it. Blocked by: portps5-w1re, portps5-l77s (memory_budget support in the capability table).

## Higher Goal

Memory pressure is visible in every results JSON before it becomes a stall.

## Acceptance Criteria

- [ ] Per-second numeric mem event: VRAM used/budget, guest committed bytes, host-import bytes, staging bytes
- [ ] Results JSON memory_peak_mb fields
- [ ] Unit test with a fake budget source

## Out of Scope

Eviction (portps5-hps3).

## Summary of Changes

TBD
