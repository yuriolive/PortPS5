---
# portps5-9u7e
title: Offline stub for libScePlayerInvitationDialog
status: todo
type: feature
created_at: 2026-10-01T20:53:13Z
updated_at: 2026-10-01T20:53:13Z
---

## Context

Only listed in the sysmodule table; AnyPS5 has a header and no implementation. The TMNT dump references it. Blocked by: none.

## Higher Goal

Dialog calls return scripted, logged offline results like the other dialogs (save-data.md), never block.

## Acceptance Criteria

- [ ] Exports with documented offline results and SCE codes
- [ ] GoogleTest for init/open/status/close
- [ ] save-data.md dialog list updated

## Out of Scope

Online invitations.

## Summary of Changes

TBD
