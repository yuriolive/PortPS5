---
# portps5-axx6
title: 'Fuzzing: ELF reader and relinker, PM4 parser, recompiler decoder'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T19:01:23Z
updated_at: 2026-10-01T19:01:39Z
parent: portps5-7n6b
blocked_by:
    - portps5-pbj2
---

## Context

The relinker, PM4 parser and recompiler decoder parse untrusted dump-derived input. Wrap-free fixes (#64) show the bug class. libFuzzer is not available for MinGW. Blocked by: portps5-pbj2 (Linux sanitizer job, so fuzz targets build with clang/GCC sanitizers on Linux).

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] libFuzzer or AFL++ harnesses for ElfReader, the PM4 packet parser and the RDNA2 decoder, with synthetic seed corpora only
- [ ] Short fuzz run per PR on Linux (time-boxed), longer run in the nightly workflow
- [ ] Every crash found becomes a regression test

## Out of Scope

Fuzzing with game-derived inputs in CI (local only).

## Summary of Changes

TBD
