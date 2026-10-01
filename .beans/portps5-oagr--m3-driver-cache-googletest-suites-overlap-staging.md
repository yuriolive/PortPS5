---
# portps5-oagr
title: 'M3: driver cache GoogleTest suites (overlap, staging exhaustion, descriptor lifecycle)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:26:45Z
updated_at: 2026-10-01T18:27:11Z
parent: portps5-oo21
blocked_by:
    - portps5-hkwd
---

## Context

ROADMAP M3 item (M3: driver cache GoogleTest suites (overlap, staging exhaustion, descriptor lifecycle)). Owner spec: gpu-driver.md. Blocked by: portps5-hkwd.

## Higher Goal

Close the ROADMAP M3 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Buffer/texture overlap invalidation tests
- [ ] Staging ring exhaustion tests
- [ ] Descriptor set lifecycle tests, adapted from DXVK and RPCS3 patterns with licences recorded
- [ ] ROADMAP and gpu-driver.md checkboxes ticked in the same PR

## Out of Scope

Other M3 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
