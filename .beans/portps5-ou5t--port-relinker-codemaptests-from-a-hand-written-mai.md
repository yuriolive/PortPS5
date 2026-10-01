---
# portps5-ou5t
title: Port relinker CodeMapTests from a hand-written main() runner to GoogleTest
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:53:43Z
updated_at: 2026-10-01T18:53:43Z
parent: portps5-7dk3
---

## Context

core/relinker/relinker/tests/CodeMapTests.cpp uses a hand-written main() runner, against AGENTS.md rule 11 (GoogleTest via portps5_add_gtest). Blocked by: none.

## Higher Goal

Every C++ test runs under GoogleTest discovery.

## Acceptance Criteria

- [ ] Each case becomes a TEST with invariant comments
- [ ] Registered with portps5_add_gtest; expected-count updated
- [ ] Same assertions, none weakened

## Out of Scope

New relinker cases.

## Summary of Changes

TBD
