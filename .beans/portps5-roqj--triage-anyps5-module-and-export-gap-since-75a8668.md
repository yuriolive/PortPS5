---
# portps5-roqj
title: Triage AnyPS5 module and export gap since 75a8668 (progress 63% vs 77%)
status: todo
type: task
created_at: 2026-10-01T20:54:55Z
updated_at: 2026-10-01T20:54:55Z
---

## Context

`tools/progress.py` on 2026-10-01: PortPS5 `main` 1287/2030 library exports (63.4%), 591/1166 shader opcodes (50.7%); AnyPS5 `main@709d7fe` 2333/3014 (77.4%), 604/1166 (51.8%). Upstream-only modules: libRenoirCore.PS5, libSceAmpr, libSceFont-module, libSceFontFt-module, libSceHmd, libSceJson2 (portps5-j5uf), libSceMsgDialog.native, libSceNpCppWebApi, libSceNpUtility, libScePlayerInvitationDialog (portps5-9u7e), libScePlayerSelectionDialog, libScePngEnc (portps5-k0r8), libScePsml_debug, libSceShare, libSceVideoRecordingP, libSceVoiceChat, libcohtml, libfmod, libfmodstudio, ulobjmgr. Larger export deltas: libSceAgc +88, libkernel +87, libc +81, libSceHttp/Http2 +69, libSceFont/FontFt +122. Blocked by: none.

## Higher Goal

Port upstream work driven by what gate titles import, under PortPS5 rules (no throws across APS5_VABI, no title-specific code, no APS5_* switches), not by raw export counts.

## Acceptance Criteria

- [ ] For each upstream-only module and large delta: needed by a gate title's import inventory (portps5-zadg, portps5-3eh1)? Record port / defer / skip with reason in this bean
- [ ] Middleware replacements (libfmod, libfmodstudio, libcohtml, libRenoirCore, ulobjmgr) assessed separately: they replace title-shipped third-party libraries, so check licence and whether a general mechanism is possible before any port
- [ ] One bean per module chosen for porting, with blockers

## Out of Scope

Porting itself.

## Summary of Changes

TBD
