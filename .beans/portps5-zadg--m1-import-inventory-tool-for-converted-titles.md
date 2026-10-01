---
# portps5-zadg
title: 'M1: Import inventory tool for converted titles'
status: todo
type: task
priority: normal
created_at: 2026-09-30T23:49:53Z
updated_at: 2026-09-30T23:49:53Z
---

## Context

The Dreaming Sarah inventory (815 relocation refs, 484 unique NIDs, 484/484 resolved at link time) was produced by a local script kept outside the repo; the relinker only emits ConversionReport counters (relinker/output/ConversionReport.hpp). The ROADMAP M1 item needs a repeatable tool. Spec: docs/spec/relinker.md Open questions 6 and 7.

## Higher Goal

Repeatable, evidence-backed import inventories per gate title, with no game data or vendored NID registries (ROADMAP M1 inventory item).

## Acceptance Criteria

- [ ] Tool lists unique NIDs and whether each resolves to a built prx export, reading only the converted output
- [ ] Output holds counts, NIDs and pass/fail only, never bytes or names from a dump
- [ ] GoogleTest on a synthetic ELF fixture
- [ ] relinker.md Open question 6 cites the tool

## Out of Scope

Identifying unknown NIDs by candidate-name hashing (Open question 7).

## Summary of Changes

TBD
