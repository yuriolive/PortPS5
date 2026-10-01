---
# portps5-ba7d
title: 'Results JSON: count Unsupported() aborts and the first missing export per run'
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:01:26Z
updated_at: 2026-10-01T19:07:43Z
parent: portps5-7n6b
blocked_by:
    - portps5-w1re
    - portps5-4bkt
---

## Context

The compat list needs a status (boots, in-game, playable) and the blocker. Unsupported() aborts name our own library export, which is project data, not game data. Blocked by: portps5-w1re, portps5-4bkt (the generator must exist before it gains tiers).

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] Unsupported() writes a numeric telemetry event with an export id from our own export table
- [ ] Results JSON gets unsupported_first and unsupported_count
- [ ] Compatibility list (portps5-4bkt) shows status tiers: nothing, boots, intro, in-game, playable

## Out of Scope

Game-specific notes.

## Summary of Changes

TBD
