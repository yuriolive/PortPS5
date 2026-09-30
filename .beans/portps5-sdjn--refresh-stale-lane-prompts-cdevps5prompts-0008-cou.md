---
# portps5-sdjn
title: Refresh stale lane prompts (C:\dev\ps5\prompts 00/08 counts)
status: todo
type: task
priority: normal
tags:
    - beads:portps5-56
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-09-30T22:53:51Z
---

## Description

prompts/00-conversion-runtime-inventory.md and prompts/08-stub-hardening.md still cite the superseded static estimate (467/485 resolve, 18 missing). Link-level truth: 478/484 resolve, 6 named libc locale gaps (in-scope for 08 per its own condition), 12 others resolved-or-phantom. Update both prompts so lane agents start from correct numbers.

## Acceptance Criteria

00/08 cite 478/484 + 6 named gaps; 08 pulls the 6 in-scope

Migrated from beads `portps5-56`.
