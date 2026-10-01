---
# portps5-p4mx
title: 'M1: offline PlayGo, ConvertKeycode and sysmodule unload policy'
status: completed
type: task
priority: normal
created_at: 2026-10-01T05:00:00Z
updated_at: 2026-10-01T05:00:00Z
---

## Context

Every `libScePlayGo` export threw `NotImplemented`, `libSceConvertKeycode` did not exist, and `sceSysmoduleUnloadModule` threw `std::runtime_error` for an id the project does not know even though loading such an id is accepted. Upstream AnyPS5 `22b53e3f` and `a2520aa0` depend on PlayGo and sysmodule code this tree lacks, and its PlayGo error codes (0x80B20004/05/0D) disagree with KytyPS5 and shadPS4.

## Higher Goal

Gate titles never abort on small system-library calls at boot (ROADMAP M1), with SCE error codes matching the public oracles.

## Acceptance Criteria

- [x] PlayGo implemented offline with the title's own chunk definitions and oracle error codes
- [x] `libSceConvertKeycode` keyboard-type query, with the unaudited export failing loudly
- [x] Unknown-id unload is a no-op
- [x] GoogleTest suites; sysmodule test proven to fail without the fix
- [x] Spec updated, VoiceChat skip recorded

## Out of Scope

`libSceVoiceChat` (error code unverifiable), real download semantics, the other throws in libSceSysmodule.

## Summary of Changes

`libScePlayGo/Export.cpp` + `PlayGoInternal.hpp`, `libSceConvertKeycode/Export.cpp`, `libSceSysmodule/Export.cpp`, `tests/modules/*`, `tests/CMakeLists.txt`, `docs/spec/save-data.md`.
