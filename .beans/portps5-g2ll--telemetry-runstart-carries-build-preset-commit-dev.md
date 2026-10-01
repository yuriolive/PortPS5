---
# portps5-g2ll
title: 'Telemetry: run.start carries build preset, commit, device, driver, present mode and resolution scale'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:31:56Z
updated_at: 2026-10-01T19:20:31Z
parent: portps5-r8mh
blocked_by:
    - portps5-w1re
---


## Context

Results JSON takes gpu_vendor, driver_version and commit from runner flags, and has no build preset, present mode or resolution scale; regress compare (portps5-rrll) must refuse mismatched conditions. Blocked by: portps5-w1re (config startup landed in PR #71).

## Higher Goal

Run conditions come from the runtime itself, so before/after comparisons are mechanical and can't be mislabelled.

## Acceptance Criteria

- [ ] run.start adds numeric/enum fields: build preset id, git commit (short hash baked at build), Vulkan vendor id, driver version, present mode, resolution scale
- [ ] tools/regress.py prefers runtime values over flags and warns on disagreement
- [ ] verification.md 4.1 updated

## Out of Scope

Host CPU model strings (personal hardware detail, PRD 4.4).

## Summary of Changes

TBD
