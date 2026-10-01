---
# portps5-10fr
title: 'M2: Mount per-title /savedata0 with sandboxed file I/O (PR #53)'
status: in-progress
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

- [ ] PR #53 reviewed and rebased on main (the merge itself lands this bean on main as completed)
- [x] Traversal, colon, unmounted and title-id cases covered by GoogleTest
- [x] save-data.md Open questions and M2 row updated

## Out of Scope

Multi-slot save dialog UI, encrypted PFS, cloud backup.

## Summary of Changes

Per-title `/savedata0` mount with sandboxed file I/O in libkernel, `SaveDataMountTests`, `docs/spec/save-data.md` M2 row and `docs/TESTING.md` updated (PR #53).
