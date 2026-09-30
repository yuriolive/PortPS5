---
# portps5-421p
title: 'M1: Adapted GuestArena extent-tree port'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-7
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-09-30T22:53:51Z
---

## Description

Trial-ported on PR #28 and reverted: O(n) scan contradicts the extent-tree decision, throws break no-throw rule. Return with pins, page-state table, return codes per docs/spec/guest-memory.md.

## Acceptance Criteria

M1 guest-memory box checked, arena benchmarks green

Migrated from beads `portps5-7`.
