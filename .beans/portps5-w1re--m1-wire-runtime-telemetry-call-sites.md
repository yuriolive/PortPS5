---
# portps5-w1re
title: 'M1: Wire runtime telemetry call sites (Start, NotePresent, guest progress)'
status: todo
type: task
priority: high
created_at: 2026-10-01T03:00:00Z
updated_at: 2026-10-01T19:20:18Z
parent: portps5-r8mh
---



## Context

The telemetry core (bean portps5-f9a3) exists but nothing calls `PortPS5_Telemetry_Start_nid_no_patch` at process start-up, the presenter does not call `NotePresent` or `SetVideoLatencyMs`, and no guest thread hook calls `NoteGuestProgress`. Until then `tools/regress.py run` finds no telemetry log.

## Higher Goal

Every local run produces the M2 gate metrics (ROADMAP M1 telemetry, PRD F9).

## Acceptance Criteria

- [ ] Start called once at start-up after config load (config startup landed in PR #71), with resolution, cache state and audio device
- [ ] Presenter calls NotePresent and SetVideoLatencyMs per present (coordinate with the AGC driver work)
- [ ] A guest thread progress site calls NoteGuestProgress
- [ ] `pipeline.create`, `spirv.compile`, `warmup.end` and `dialog.open` events emitted at their sites
- [ ] A queue registers the diagnostics hook

## Out of Scope

Results JSON schema changes.

## Summary of Changes

TBD
