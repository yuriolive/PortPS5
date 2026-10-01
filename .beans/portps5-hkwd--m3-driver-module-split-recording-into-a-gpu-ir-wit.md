---
# portps5-hkwd
title: 'M3: driver module split recording into a GPU IR with resource states'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:12:42Z
updated_at: 2026-10-01T18:15:25Z
parent: portps5-4ut1
blocked_by:
    - portps5-tiod
---

## Context

The M3 module split (ROADMAP M3, docs/spec/gpu-driver.md Decision) records into a GPU IR: a recorded command list with explicit per-resource states and a queue tag, lowered to Vulkan afterwards. Blocked by: portps5-tiod (the submit path must go through the Recorder first).

## Higher Goal

Barrier optimization, multithreaded recording, async compute, RT passes and frame overlap can be added later without rewriting the CommandProcessor (performance track P2, 2.0 M9-M10).

## Acceptance Criteria

- [ ] CommandProcessor, Recorder, Buffer/Texture/Pipeline caches, Rasterizer, Presenter as separate modules
- [ ] GPU IR with resource states and queue tag; one queue and single-threaded recording
- [ ] Barriers derived from IR resource states, not ad hoc
- [ ] lavapipe tests: IR lowering emits the expected barriers for synthetic PM4
- [ ] gpu-driver.md M3 row ticked

## Out of Scope

Async compute queue, multithreaded recording (M5 perf pass or 2.0).

## Summary of Changes

TBD
