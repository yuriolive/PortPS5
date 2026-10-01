---
# portps5-ks96
title: Exercise sce_module guest-module path for PPSA02929 (conversion used --skip-sce-module)
status: todo
type: task
priority: normal
tags:
    - beads:portps5-54
created_at: 2026-09-30T22:53:50Z
updated_at: 2026-10-01T18:33:24Z
parent: portps5-7dk3
---


## Context

The Dreaming Sarah (PPSA02929) conversion probe ran with `--skip-sce-module`, and the app ships an `sce_module` directory whose processing is untested for this title. Migrated from beads `portps5-54`. Spec: docs/spec/relinker.md (guest-module path).

## Higher Goal

Do not skip a conversion step silently for a gate title: either it works or the failure is recorded and owned.

## Acceptance Criteria

- [ ] Re-run conversion without `--skip-sce-module`
- [ ] Guest artifacts verified and boot-staged, or the path recorded as unnecessary in relinker.md
- [ ] Failure, if any, recorded with an owner

## Out of Scope

Modifying the `sce_module` builder without a failing case.

## Summary of Changes

TBD
