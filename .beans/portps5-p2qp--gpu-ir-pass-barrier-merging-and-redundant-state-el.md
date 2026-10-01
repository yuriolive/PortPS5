---
# portps5-p2qp
title: 'GPU IR pass: barrier merging and redundant state elimination'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:25:12Z
updated_at: 2026-10-01T18:32:58Z
parent: portps5-7fqk
blocked_by:
    - portps5-hkwd
    - portps5-hfiw
---

## Context

The GPU IR records explicit resource states; nothing merges barriers or removes redundant state yet. Blocked by: portps5-hkwd, portps5-hfiw (to measure).

## Higher Goal

Performance track (ROADMAP, PRD 4.5) GPU idle time and CPU record cost reduced.

## Acceptance Criteria

- [ ] Pass merges adjacent barriers and drops redundant state binds on the IR
- [ ] Lavapipe tests: emitted barriers are a superset-safe minimum for synthetic sequences
- [ ] Before/after gpu_ms and rec_ms on a perf scene

## Out of Scope

Multithreaded recording.

## Summary of Changes

TBD
