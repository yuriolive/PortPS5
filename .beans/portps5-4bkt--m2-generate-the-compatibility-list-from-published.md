---
# portps5-4bkt
title: 'M2: generate the compatibility list from published results JSON'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:44Z
updated_at: 2026-10-01T19:07:24Z
parent: portps5-dbpx
blocked_by:
    - portps5-3m3u
---


## Context

ROADMAP M2 item (M2: generate the compatibility list from published results JSON). Owner spec: verification.md. Blocked by: portps5-3m3u.

## Higher Goal

Close the ROADMAP M2 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Generator reads compat/results/**.json and writes the list (metrics and pass/fail only)
- [ ] Lists pass/fail per title; status tiers (nothing to playable) come later from portps5-ba7d, which owns them
- [ ] pytest on synthetic results
- [ ] ROADMAP M2 exit item ticked once Dreaming Sarah and TMNT results are published
- [ ] ROADMAP and verification.md checkboxes ticked in the same PR

## Out of Scope

Other M2 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
