---
# portps5-7qon
title: Dialogs emit a numeric dialog.open telemetry event instead of logging guest text
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:31:58Z
updated_at: 2026-10-01T18:31:58Z
parent: portps5-r8mh
blocked_by:
    - portps5-w1re
---

## Context

verification.md lists dialog.open but dialogs only log; MsgDialog logs guest text to the run log. Blocked by: portps5-w1re.

## Higher Goal

Dialog activity is measurable and no game text reaches logs.

## Acceptance Criteria

- [ ] Each dialog Open emits Event dialog.open {kind}
- [ ] MsgDialog stops logging guest text outside [debug] tracing
- [ ] GoogleTest on the emitted record

## Out of Scope

Dialog behaviour changes.

## Summary of Changes

TBD
