---
# portps5-0mv7
title: 'M1: Exit verification on the reference tier (Demon''s Souls intro, perf stats, frame comparison)'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-5
created_at: 2026-09-30T22:53:49Z
updated_at: 2026-10-01T18:33:23Z
parent: portps5-7dk3
---


## Context

The original description of this bean was migrated from an unrelated beads note (progress-report deltas, PR #28). The real scope is the M1 exit criterion: local verification on the reference tier, meaning title boot, the performance gate and frame-capture comparison, on a maintainer machine with their own dumps (docs/spec/verification.md section 2, PRD section 4.3). No local run has been recorded in the repo. Migrated from beads `portps5-5`.

## Higher Goal

Prove M1 with evidence: each gate title converts, boots and reports telemetry, recorded only as results JSON (legal boundary: no frames, footage or dumps).

## Acceptance Criteria

- [ ] Regression tooling exists: portps5-3m3u
- [ ] Dreaming Sarah boots to the title screen and its result JSON is recorded: portps5-kmb6
- [ ] Demon's Souls reaches the in-engine intro cinematic with title-specific code removed (ROADMAP M1 exit)
- [ ] Performance gate: a recorded run on the reference tier meets the PRD 4.3 bar for the stage reached (the old title's 60 fps figure is superseded by the 30 fps average / 20 fps 1% low bar) and the frame-time stats appear in the results JSON
- [ ] Frame-capture comparison: deterministic frames match the local per-title references (SSIM >= 0.99), kept local, only the pass/fail result recorded
- [ ] Hosted golden corpus covers every decoded instruction class and the local game-derived corpus replays with 0 validation failures (ROADMAP M1 exit)

## Out of Scope

Full-run pass bar (M2 and later), compatibility list generation.

## Summary of Changes

TBD
