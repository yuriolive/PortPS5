---
# portps5-sdjn
title: Refresh stale lane prompts (C:\dev\ps5\prompts 00/08 counts)
status: todo
type: task
priority: normal
tags:
    - beads:portps5-56
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-09-30T23:56:59Z
---

## Context

The lane prompts outside the repository (`C:\dev\ps5\prompts`, `00-conversion-runtime-inventory.md` and `08-stub-hardening.md`) still cite the superseded static estimate (467/485 resolve, 18 missing). After PRs #44 and #48 the recorded link-level truth for Dreaming Sarah is 484/484 NIDs resolved (docs/spec/relinker.md Open question 6). This is a maintainer-local task: the prompts are not in this repository and were not audited here. Migrated from beads `portps5-56`.

## Higher Goal

Lane agents start from correct numbers.

## Acceptance Criteria

- [ ] Prompts 00 and 08 cite 484/484 resolved at link time, with the 6 locale/iostream symbols noted as resolved by PR #48
- [ ] Prompt 08 no longer lists the six named gaps as open

## Out of Scope

Anything inside the repository.

## Summary of Changes

TBD
