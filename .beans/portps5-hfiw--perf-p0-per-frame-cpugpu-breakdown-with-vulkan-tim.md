---
# portps5-hfiw
title: 'Perf P0: per-frame CPU/GPU breakdown with Vulkan timestamp queries'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:03:34Z
updated_at: 2026-10-01T18:10:17Z
parent: portps5-7fqk
blocked_by:
    - portps5-w1re
    - portps5-tiod
---

## Context

The telemetry `frame` record carries only `dt_ms`, so a slow frame can't be attributed to guest CPU, driver recording, GPU execution, fence waits or present. Target design is in docs/spec/verification.md §4.4 ("Frame breakdown"). Blocked by: portps5-w1re (the presenter must call NotePresent) and portps5-tiod (GPU timestamps wrap the Recorder submit path).

## Higher Goal

Every performance decision in the track (ROADMAP P1–P3, gpu-driver open questions 2, 3 and 11) is made from a measured per-frame breakdown, not from intuition.

## Acceptance Criteria

- [ ] Vulkan timestamp queries around each frame's submits, converted with `timestampPeriod`; omitted when the queue has no timestamp support
- [ ] `frame` record gains numeric `cpu_ms`, `rec_ms`, `gpu_ms`, `wait_ms`, `present_ms`
- [ ] `tools/regress.py` writes `frame_breakdown_ms` (per-frame means)
- [ ] GoogleTest on the timestamp conversion and record schema (synthetic clock); lavapipe test that a submit yields a `gpu_ms` value
- [ ] verification.md §4.4 checkbox ticked

## Out of Scope

Per-pass GPU timelines and markers (later, behind `[debug] profile`). Violation counters (portps5-aifo).

## Summary of Changes

TBD
