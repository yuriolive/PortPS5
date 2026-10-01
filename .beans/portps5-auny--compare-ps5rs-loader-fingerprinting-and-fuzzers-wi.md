---
# portps5-auny
title: Compare ps5rs loader, fingerprinting and fuzzers with PortPS5
status: todo
type: task
created_at: 2026-10-01T22:19:53Z
updated_at: 2026-10-01T22:19:53Z
parent: portps5-7n6b
---

## Context

ps5rs (github.com/claimore22/ps5rs, GPL-2.0, Rust) parses SELF/ELF/PRX, resolves NID imports, runs a virtual loader that applies relocations and resolves imports against PRX exports, fingerprints binaries for engine and middleware, and has cargo-fuzz targets (fuzz/). Local copy at C:/dev/ps5/ps5rs. Its data/ files (nids.csv, stubs.txt) include firmware-derived sources and stay local (only counts are quoted). Blocked by: none.

## Higher Goal

Borrow proven checks and test inputs for the relinker and the planned fuzzing, and learn which middleware the gate titles use.

## Acceptance Criteria

- [ ] Compare ps5rs relocation types and import resolution with core/relinker; list relocation types or edge cases PortPS5 does not handle, each as a bean with a synthetic test
- [ ] Review ps5rs fuzz targets and seed strategy for ELF/PRX parsing; feed the findings into portps5-axx6 (behaviour only: Rust code is not copied into the C++ tree)
- [ ] Run ps5rs fingerprinting locally on the five gate dumps; record engine and middleware per title in the import-inventory notes (names only, no game data in the repo)
- [ ] Note any NID-name sources it has that our tools lack

## Out of Scope

Porting Rust code. Committing ps5rs data files.

## Summary of Changes

TBD
