---
# portps5-dtiq
title: 'Driver upload engine: staging ring on timeline semaphores, batched copies, optional transfer queue'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:25:10Z
updated_at: 2026-10-01T18:32:58Z
parent: portps5-7fqk
blocked_by:
    - portps5-hkwd
---

## Context

Uploads are submitted ad hoc; PR #77 lists batched reads/uploads as later slices. Blocked by: portps5-hkwd.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) invariants P1/P5: batched uploads without CPU waits.

## Acceptance Criteria

- [ ] Staging ring on timeline semaphores, reuse after GPU completion
- [ ] Copies batched per submit; optional dedicated transfer queue from the capability table
- [ ] Ring exhaustion and ordering tests on lavapipe

## Out of Scope

Residency eviction (portps5-hps3).

## Summary of Changes

TBD
