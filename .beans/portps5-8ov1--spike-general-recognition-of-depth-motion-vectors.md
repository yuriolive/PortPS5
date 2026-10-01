---
# portps5-8ov1
title: 'Spike: general recognition of depth, motion vectors and jitter on the GPU IR (PRD V-R6)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:23:41Z
updated_at: 2026-10-01T18:33:01Z
parent: portps5-fxpf
blocked_by:
    - portps5-hkwd
---

## Context

Temporal upscaling and frame generation need depth, motion vectors and jitter, which a translated PS5 renderer does not label (PRD V-R6). Blocked by: portps5-hkwd (GPU IR).

## Higher Goal

Decide whether a general recogniser on the GPU IR exists before M12 commits to temporal features.

## Acceptance Criteria

- [ ] Survey which IR facts identify depth/MV/jitter generally (formats, usage, per-frame projection deltas)
- [ ] Prototype on synthetic IR; observe on local dumps (counts only, no game data committed)
- [ ] Recommendation in gpu-driver.md open questions

## Out of Scope

Implementation (M12).

## Summary of Changes

TBD
