---
# portps5-s9zz
title: 'CI: PE structure validation of relinker output with pefile'
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:01:22Z
updated_at: 2026-10-01T19:01:39Z
parent: portps5-7n6b
---

## Context

No test asserts PE structure (section alignment, export directory, base relocations, .pdata/.ehfram unwind data, subsystem) of relinked output. Blocked by: none.

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] pytest using pefile (MIT, test-only dependency in pyproject dev group) over relinker outputs of the synthetic test ELFs
- [ ] Checks: headers and alignment, imports resolve to existing prx exports, unwind data present for every code section, GUI/CUI subsystem flag
- [ ] Runs in python_quality and through ctest

## Out of Scope

Runtime loading (bean portps5-ktrt).

## Summary of Changes

TBD
