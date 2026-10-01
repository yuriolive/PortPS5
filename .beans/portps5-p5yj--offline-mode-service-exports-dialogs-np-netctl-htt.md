---
# portps5-p5yj
title: 'Offline-mode service exports: dialogs, NP, NetCtl, HTTP/2, SSL'
status: todo
type: task
created_at: 2026-10-01T21:42:54Z
updated_at: 2026-10-01T21:42:54Z
parent: portps5-dbpx
---

## Context

Imported and stubbed or missing: ErrorDialog `Initialize`, `Open`, `UpdateStatus`, `Terminate` (KytyPS5 `libDialog.cpp:145-148`); NpCommerce `DialogInitialize`, `DialogOpen`, `DialogTerminate` (AnyPS5 `libSceNpCommerce/Export.cpp`); NpSessionSignaling `CreateContext2`, `DestroyContext`, `ActivateSession`, `Deactivate`, `GetConnectionInfo`, `GetConnectionStatus` (AnyPS5 `libSceNpSessionSignaling/Export.cpp:23`, the rest have no reference); NetCtl `CheckCallback`, `GetResult`, `RegisterCallback` (AnyPS5 `libSceNetCtl/Export.cpp:98-121`); `sceHttp2Init`, `sceSslInit` (AnyPS5); `sceNetSendmsg`, `sceNetRecvmsg`, `sceNpWebApi2AddWebTraceTag` (shadPS4); libScePad NID `n3kSX62fgNo` with no known name (KytyPS5 `libPad.cpp:221`). PlayerInvitationDialog is portps5-9u7e.

Source: NID gap analysis of a local, non-gate dump (PPSA04489), matched against built PortPS5 prx export tables at main@5dd65fe3 and the reference trees AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43 and shadPS4 fecfbed0. Only NIDs and function names were read from the dump. 'Imported' does not mean 'called': confirm against the gate-title inventories (portps5-zadg, portps5-3eh1) before porting. Licences: AnyPS5 and KytyPS5 GPL-2.0, shadPS4 GPL-2.0-or-later (port with the file's copyright header and cite the commit); SharpEmu is C#, so it is a behavioural reference only.

Blocked by: none.

## Higher Goal

Online-facing calls report "offline" through their real return codes, so titles take their offline path (PRD F8) instead of aborting.

## Acceptance Criteria

- [ ] Each export returns the code the console returns when signed out or offline, documented per library
- [ ] Dialogs open and finish with a cancelled or offline result through the existing dialog state machine, emitting the numeric dialog.open event (portps5-7qon)
- [ ] Pad `n3kSX62fgNo` behaviour taken from KytyPS5 and documented in input.md
- [ ] GoogleTest per export

## Out of Scope

Any online service.
