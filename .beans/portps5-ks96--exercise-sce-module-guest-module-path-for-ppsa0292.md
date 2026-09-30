---
# portps5-ks96
title: Exercise sce_module guest-module path for PPSA02929 (conversion used --skip-sce-module)
status: todo
type: task
priority: normal
tags:
    - beads:portps5-54
created_at: 2026-09-30T22:53:50Z
updated_at: 2026-09-30T22:53:50Z
---

## Description

Dreaming Sarah conversion probe ran with --skip-sce-module; the app ships a sce_module dir whose processing is untested for this title. Re-run conversion without the flag, verify guest artifacts, and boot-stage. If the path is unnecessary, record why in docs/spec/relinker.md.

## Acceptance Criteria

Conversion without --skip-sce-module succeeds or fails with a recorded, owned error

Migrated from beads `portps5-54`.
