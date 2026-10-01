---
# portps5-vj60
title: 'M5: performance pass against the bar'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:26:48Z
updated_at: 2026-10-01T18:27:14Z
parent: portps5-bxvu
blocked_by:
    - portps5-hfiw
    - portps5-aifo
    - portps5-rrll
    - portps5-52bs
---

## Context

ROADMAP M5 item (M5: performance pass against the bar). Owner spec: gpu-driver.md, pipeline-cache.md, threading.md. Blocked by: portps5-hfiw, portps5-aifo, portps5-rrll, portps5-52bs.

## Higher Goal

Close the ROADMAP M5 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Every remaining PRD 4.5 violation removed or justified in its owner spec
- [ ] Open questions decided with data: async compute, HTILE/DCC, multithreaded recording, pipeline libraries, core placement, large pages
- [ ] Before/after results JSON for each change
- [ ] ROADMAP and gpu-driver.md, pipeline-cache.md, threading.md checkboxes ticked in the same PR

## Out of Scope

Other M5 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
