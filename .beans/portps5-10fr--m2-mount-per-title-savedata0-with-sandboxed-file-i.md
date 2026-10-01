---
# portps5-10fr
title: 'M2: Mount per-title /savedata0 with sandboxed file I/O (PR #53)'
status: todo
type: feature
priority: high
created_at: 2026-09-30T23:46:59Z
updated_at: 2026-09-30T23:46:59Z
---

## Context

Guest opens of /savedata0 are not mapped to host storage on main (no savedata0 mount exists under core/). PR #53 (open, not landed) adds a per-title container mounted when param.json loads, EACCES on .. or : components, SCE error codes from sceKernelOpen/Read/Write/Lseek/Close/Stat/Unlink/Mkdir/Fsync instead of throws, and SaveDataMountTests. Spec: docs/spec/save-data.md.

## Higher Goal

PRD F2: a gate title can write and read its /savedata0 files across runs, inside a sandbox.

## Acceptance Criteria

- [ ] PR #53 reviewed, rebased on main and merged
- [ ] Traversal, colon, unmounted and title-id cases covered by GoogleTest
- [ ] save-data.md Open questions and M2 row updated

## Out of Scope

Multi-slot save dialog UI, encrypted PFS, cloud backup.

## Summary of Changes

TBD
