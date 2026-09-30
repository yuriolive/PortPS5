---
# portps5-08gr
title: 'Tools: prove or drop V_ADD_NC_I32/V_SUB_NC_I32 decoder aliases'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-9
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-09-30T22:53:51Z
---

## Description

Macroscope thread on PR #28 (tools/rdna_isa.txt:708) left open: enum has VAddI32/VSubI32 but nothing references them. Needs opcode-level proof (VOP3 table mapping or golden decode) before aliasing; otherwise close as invalid so badges stay honest.

## Acceptance Criteria

Thread resolved with proof commit or wontfix note

Migrated from beads `portps5-9`.
