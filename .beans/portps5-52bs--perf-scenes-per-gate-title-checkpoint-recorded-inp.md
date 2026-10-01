---
# portps5-52bs
title: 'Perf scenes per gate title: checkpoint, recorded input, fixed duration, measured noise'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:25:12Z
updated_at: 2026-10-01T19:01:40Z
parent: portps5-7fqk
blocked_by:
    - portps5-3m3u
---

## Context

Before/after performance claims need repeatable runs; full runs are long and noisy. Blocked by: portps5-3m3u (regress runner checkpoint replay).

## Higher Goal

Every performance PR is measured on the same short, repeatable scene.

## Acceptance Criteria

- [ ] Per gate title: save checkpoint + recorded input + fixed duration, all kept local
- [ ] Run-to-run noise measured (5 runs) and recorded; rrll thresholds set from it
- [ ] verification.md documents the scene protocol, including a locked environment (fixed power plan, no other load, GPU clocks locked where the vendor tool allows); no game data committed

## Out of Scope

Hosted CI runs (no GPU, no game data).

## Summary of Changes

TBD
