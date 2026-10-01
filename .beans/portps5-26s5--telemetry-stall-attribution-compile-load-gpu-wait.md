---
# portps5-26s5
title: 'Telemetry: stall attribution (compile, load, GPU wait, guest)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:31:56Z
updated_at: 2026-10-01T18:31:57Z
parent: portps5-7fqk
blocked_by:
    - portps5-hfiw
    - portps5-aifo
---

## Context

A stall (gap over 1 s) is counted but not explained. Blocked by: portps5-hfiw (breakdown), portps5-aifo (violation counters).

## Higher Goal

Every stall in a results JSON names its dominant cause, so 1% low work targets the right subsystem.

## Acceptance Criteria

- [ ] frame records over a threshold carry a numeric cause code from the breakdown and counters
- [ ] Results JSON stalls_by_cause
- [ ] pytest on synthetic logs

## Out of Scope

Fixing the stalls.

## Summary of Changes

TBD
