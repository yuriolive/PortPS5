---
# portps5-afme
title: 'M2: Enable libSceMouse exports (undisable GTest)'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-10
created_at: 2026-09-30T22:53:48Z
updated_at: 2026-09-30T22:53:48Z
---

## Description

Backend + VideoOut routing + DISABLED GTest compile on PR #28; exports are Unsupported() stubs. Implement sceMouse init/open/read/close over the backend, drop DISABLED_, remove the process-global mouseMutex. See docs/spec/input.md.

## Acceptance Criteria

Mouse tests run green in CI, M2 input matrix records mouse

Migrated from beads `portps5-10`.
