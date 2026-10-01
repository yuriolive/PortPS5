---
# portps5-kmb6
title: 'M2: Dreaming Sarah full-run protocol (gate 1)'
status: todo
type: task
priority: high
created_at: 2026-09-30T23:52:22Z
updated_at: 2026-10-01T17:59:26Z
blocked_by:
    - portps5-ks96
---


## Context

Dreaming Sarah (PPSA02929) is gate title 1. Its conversion and link-level inventory are recorded (docs/spec/relinker.md Open question 6), the Config::Loader and locale NID blockers are fixed (PRs #44, #47, #48), but no boot or full run has been recorded. The protocol and pass bar are in docs/PRD.md section 4.3 and docs/spec/verification.md section 3.

## Higher Goal

ROADMAP M2 exit: average at least 30 fps, 1% low at least 20 fps at 1080p, 0 crashes, 0 softlocks, save round-trip.

## Acceptance Criteria

- [ ] Convert without --skip-sce-module (see the sce_module bean) and boot to the title screen
- [ ] Record a regression result JSON with the compat-result skill
- [ ] One full run meets the pass bar, results JSON published, no game data committed

## Out of Scope

TMNT full run (same milestone, separate run).

## Summary of Changes

TBD
