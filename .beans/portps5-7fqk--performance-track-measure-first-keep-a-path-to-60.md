---
# portps5-7fqk
title: 'Performance track: measure first, keep a path to 60 fps open'
status: todo
type: epic
priority: normal
created_at: 2026-10-01T18:03:34Z
updated_at: 2026-10-01T18:10:17Z
---

## Context

The 1.0 bar is 30 fps (PRD §4.3), but per-draw performance work was already landing (PRs #71, #74, #75, #77) with no roadmap home, and the results JSON measures only fps. PRD §4.5 now defines steady-state invariants P1–P6, and the ROADMAP "Performance track" section orders the work in four steps: P0 measure, P1 per-draw cost, P2 GPU-side resolution, P3 the M5 performance pass. Blocked by: none (the children carry their own blockers).

## Higher Goal

No subsystem reaches 1.0 with a design that rules out 60 fps or the v2 open-world tier. Every optimization is measured, never guessed.

## Acceptance Criteria

- [ ] P0: portps5-w1re, portps5-rrll, portps5-hfiw, portps5-aifo completed
- [ ] P1: PRs #71, #75, #77 landed or closed with reasons; portps5-r7qk decided
- [ ] P2: M3 exit check on the P1 and P2 counters (ROADMAP Performance track)
- [ ] P3: every remaining invariant violation removed or justified in its owner spec (M5)

## Out of Scope

60/120 fps targets, upscalers, frame-rate unlocks (PRD §5). The v2 open-world tier.

## Summary of Changes

TBD
