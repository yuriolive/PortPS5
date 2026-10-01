---
# portps5-yhj7
title: 'Telemetry: crash event producer (unhandled exception filter writes crash and run.end)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:31:55Z
updated_at: 2026-10-01T18:31:56Z
parent: portps5-r8mh
blocked_by:
    - portps5-w1re
---

## Context

verification.md 4.1 documents a crash event but no code writes it; tools/regress.py infers crashes from a missing run.end or a non-zero exit. Blocked by: portps5-w1re (telemetry must be started).

## Higher Goal

Every crash is a structured record with a numeric reason, so crash counts don't depend on exit-code heuristics (PRD F9).

## Acceptance Criteria

- [ ] Top-level unhandled-exception filter writes crash {code} then run.end before the process dies
- [ ] No game data or addresses beyond module-relative offsets of our own libraries
- [ ] GoogleTest death test that a forced fault produces both records

## Out of Scope

Minidumps (contain game memory, local only, separate decision).

## Summary of Changes

TBD
