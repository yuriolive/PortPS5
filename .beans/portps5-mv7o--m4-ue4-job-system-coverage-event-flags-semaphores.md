---
# portps5-mv7o
title: 'M4: UE4 job-system coverage (event flags, semaphores, fibers)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:46Z
updated_at: 2026-10-01T18:27:12Z
parent: portps5-rd6g
blocked_by:
    - portps5-k34n
---

## Context

ROADMAP M4 item (M4: UE4 job-system coverage (event flags, semaphores, fibers)). Owner spec: threading.md. Blocked by: portps5-k34n.

## Higher Goal

Close the ROADMAP M4 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Every job-system import of Bugsnax resolves to a real implementation, not Unsupported()
- [ ] Perturbation tests for the event-flag and semaphore paths UE4 uses
- [ ] ROADMAP and threading.md checkboxes ticked in the same PR

## Out of Scope

Other M4 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
