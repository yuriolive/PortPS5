---
# portps5-aifo
title: 'Perf P0: invariant violation counters in telemetry (PRD 4.5)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:03:34Z
updated_at: 2026-10-01T18:10:17Z
parent: portps5-7fqk
blocked_by:
    - portps5-w1re
---

## Context

PRD §4.5 defines steady-state invariants. P1 (no unrequested CPU wait or readback), P2 (no Vulkan object creation per draw or dispatch), P3 (no compile on the submit thread) and P5 (no full compare or copy of guest memory proven unchanged) can be counted at their sites. Target design is in docs/spec/verification.md §4.4 ("Violation counters"). Blocked by: portps5-w1re (telemetry must be started and `warmup.end` emitted).

## Higher Goal

Violations become visible numbers in every results JSON, so they can't build up silently before 1.0.

## Acceptance Criteria

- [ ] Numeric `perf.violation` event (`kind`, `n`), counted only after `warmup.end`
- [ ] Count sites: fence waits not requested by the guest (P1); `vkCreate*` / `vkAllocateMemory` reached from the per-draw/dispatch path (P2); recompile or pipeline creation on the submit thread (P3); full texture/buffer revalidation compares (P5)
- [ ] `tools/regress.py` writes `invariants: { p1, p2, p3, p5 }`, not part of the pass rule
- [ ] GoogleTest per counter site through a test hook (no environment variable)
- [ ] Each known violation found on a gate title gets a bean and a line in its owner spec

## Out of Scope

Fixing the violations (performance track P1–P3).

## Summary of Changes

TBD
