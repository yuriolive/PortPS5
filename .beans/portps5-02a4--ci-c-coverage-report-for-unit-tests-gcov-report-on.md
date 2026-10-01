---
# portps5-02a4
title: 'CI: C++ coverage report for unit tests (gcov), report only'
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:01:25Z
updated_at: 2026-10-01T19:01:25Z
parent: portps5-7n6b
---

## Context

Python has a coverage gate; C++ has none. Blocked by: none.

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] ci builds a coverage variant on a schedule and uploads a summary (lines and functions per module)
- [ ] No gate at first; record the baseline in verification.md

## Out of Scope

Coverage gate.

## Summary of Changes

TBD
