---
# portps5-macl
title: 'M2: TMNT: Shredder''s Revenge full-run protocol (gate 2)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:26:44Z
updated_at: 2026-10-01T18:27:10Z
parent: portps5-dbpx
blocked_by:
    - portps5-w1re
    - portps5-3m3u
    - portps5-de24
    - portps5-8gdr
---

## Context

ROADMAP M2 item (M2: TMNT: Shredder's Revenge full-run protocol (gate 2)). Owner spec: verification.md. Blocked by: portps5-w1re, portps5-3m3u, portps5-de24, portps5-8gdr.

## Higher Goal

Close the ROADMAP M2 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Boot to title screen and record a regression results JSON (compat-result skill)
- [ ] One full run meets the PRD 4.3 bar (30 fps avg, 20 fps 1% low, 1080p, warm cache, 0 crashes, 0 softlocks, save round-trip)
- [ ] No game data committed
- [ ] ROADMAP and verification.md checkboxes ticked in the same PR

## Out of Scope

Other M2 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
