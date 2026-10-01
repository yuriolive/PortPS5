---
# portps5-e9ja
title: 'M1: libSceMsgDialog result struct, validation and auto-answer policy'
status: completed
type: task
priority: normal
created_at: 2026-10-01T03:00:00Z
updated_at: 2026-10-01T03:00:00Z
---

## Context

`libSceMsgDialog` wrote a single 32-bit button id into the caller's result buffer, so a title reading the 44-byte `SceMsgDialogResult` (mode, result, button id) saw garbage in two of its three fields. The library also ignored the parameter block entirely: progress bars and button-less dialogs finished on the first poll, `sceMsgDialogGetStatus` never advanced a dialog (a GetStatus-only poller hung), and error codes for "not finished" and "close without a dialog" were wrong or missing. Upstream AnyPS5 `2e8c1096` fixes part of this but carries wrong `ButtonType` values (focus-No is 7, not 4) and a wrong BUSY code.

## Higher Goal

Offline dialogs never block a gate title (ROADMAP M1) while returning what a console returns, so titles that read the result or the error codes behave correctly. Guest pointers are treated as untrusted.

## Acceptance Criteria

- [x] `sceMsgDialogGetResult` writes the 44-byte `SceMsgDialogResult` (mode, result, button id, zeroed reserved)
- [x] Parameter block validated (null, unreadable, unknown mode, missing sub-structure)
- [x] Auto-answer follows the focused button; button-less and progress-bar dialogs wait for `sceMsgDialogClose`
- [x] `GetStatus` and `UpdateStatus` both advance an auto-answered dialog
- [x] Every transition and error code covered by GoogleTest, proven to fail on the old implementation
- [x] `docs/spec/save-data.md` scripted-dialog table updated

## Out of Scope

A visible dialog, `libSceAvPlayer` (already ahead of upstream in this tree), `SaveDataDialog` behaviour, the guest-memory range API (bean portps5-8l0d; this change uses a Windows-only stopgap).

## Summary of Changes

`core/libs/prx/libSceMsgDialog/Export.cpp` rewritten; `tests/dialogs/MsgDialogTests.cpp` added and registered in `tests/CMakeLists.txt`; the ad-hoc `TestMsgDialog` removed from `core/libs/tests/OfflineStubs.cpp`; `docs/spec/save-data.md`.
