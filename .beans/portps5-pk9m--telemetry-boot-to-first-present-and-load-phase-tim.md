---
# portps5-pk9m
title: 'Telemetry: boot-to-first-present and load-phase timing'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:31:57Z
updated_at: 2026-10-01T18:31:57Z
parent: portps5-7fqk
blocked_by:
    - portps5-w1re
---

## Context

Softlock rules have no loading exemption (PRD 4.3) but long loads are not measured as such. Blocked by: portps5-w1re.

## Higher Goal

Load time is a tracked number, not a surprise softlock.

## Acceptance Criteria

- [ ] boot_ms from process start to first present in run.start/run.end
- [ ] Results JSON boot_ms
- [ ] pytest

## Out of Scope

Load-screen detection by title.

## Summary of Changes

TBD
