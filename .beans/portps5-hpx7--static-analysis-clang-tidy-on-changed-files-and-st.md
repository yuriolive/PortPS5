---
# portps5-hpx7
title: 'Static analysis: clang-tidy on changed files and stricter warnings (-Wcast-function-type)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:01:24Z
updated_at: 2026-10-01T19:01:24Z
parent: portps5-7n6b
blocked_by:
    - portps5-qf1m
---

## Context

Only CodeQL runs today. clang-tidy checks for function-pointer casts, calling-convention mismatches and unaligned access suit thousands of APS5_VABI exports. Blocked by: portps5-qf1m (llvm-mingw clang spike gives a clang that understands the MinGW headers).

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] clang-tidy with a curated check list on changed C++ files, as a CI step
- [ ] -Wcast-function-type added under PORTPS5_COMPILE_WARNING_AS_ERROR, existing hits fixed
- [ ] Documented in build-toolchain.md

## Out of Scope

Formatting rules.

## Summary of Changes

TBD
