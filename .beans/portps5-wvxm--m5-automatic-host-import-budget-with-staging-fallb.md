---
# portps5-wvxm
title: 'M5: automatic host-import budget with staging fallback'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:26:47Z
updated_at: 2026-10-01T18:27:13Z
parent: portps5-bxvu
blocked_by:
    - portps5-tiod
---

## Context

ROADMAP M5 item (M5: automatic host-import budget with staging fallback). Owner spec: gpu-driver.md. Blocked by: portps5-tiod.

## Higher Goal

Close the ROADMAP M5 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Budget derived from device limits and memory budget, override only through debug.gpu.host_import_mib
- [ ] Refusals fall back to staging, logged once per range class
- [ ] lavapipe test with the budget forced to 0 through a test hook
- [ ] ROADMAP and gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M5 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
