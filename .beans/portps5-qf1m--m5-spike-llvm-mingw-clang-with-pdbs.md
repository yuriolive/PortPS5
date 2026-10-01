---
# portps5-qf1m
title: 'M5 spike: llvm-mingw clang with PDBs'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:26:48Z
updated_at: 2026-10-01T18:27:14Z
parent: portps5-bxvu
---

## Context

ROADMAP M5 item (M5 spike: llvm-mingw clang with PDBs). Owner spec: build-toolchain.md. Blocked by: none.

## Higher Goal

Close the ROADMAP M5 scope with a general mechanism and a test that runs without game data where possible (PRD 4.2/4.3, .agents/rules/testing.md).

## Acceptance Criteria

- [ ] Build with llvm-mingw clang; DWARF unwinder validates across APS5_VABI frames
- [ ] Adopt only if validation passes; record result in build-toolchain.md
- [ ] ROADMAP and build-toolchain.md checkboxes ticked in the same PR

## Out of Scope

Other M5 items (separate beans). Title-specific code paths.

## Summary of Changes

TBD
