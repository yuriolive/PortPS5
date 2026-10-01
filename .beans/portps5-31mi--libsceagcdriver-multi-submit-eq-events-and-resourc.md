---
# portps5-31mi
title: 'libSceAgcDriver: multi-submit, EQ events and resource registration'
status: todo
type: feature
created_at: 2026-10-01T21:42:53Z
updated_at: 2026-10-01T21:42:53Z
parent: portps5-dbpx
---

## Context

34 imported libSceAgcDriver exports are stubbed (19) or missing (15). Submissions: `SubmitMultiDcbs`, `SubmitMultiAcbs`, `AgrSubmitMultiDcbs` (KytyPS5 `libAgcDriver.cpp:174-179`). EQ events: `AddEqEvent`, `DeleteEqEvent` (AnyPS5 `Eq/src/Event.cpp`), `GetEqContextId` (KytyPS5). Resource registration: `InitResourceRegistration`, `QueryResourceRegistrationUserMemoryRequirements`, `RegisterOwner`, `RegisterDefaultOwner`, `GetDefaultOwner`, `RegisterResource`, `RegisterWorkloadStream`, `UnregisterOwnerAndResources`, `UnregisterAllResourcesForOwner`, `UnregisterResource`, `GetResourceRegistrationMaxNameLength` (SharpEmu `AgcExports.DriverResources.cs`). 17 more, mostly resource-name and capture APIs, have no reference; the list is in the local gap report.

Source: NID gap analysis of a local, non-gate dump (PPSA04489), matched against built PortPS5 prx export tables at main@5dd65fe3 and the reference trees AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43 and shadPS4 fecfbed0. Only NIDs and function names were read from the dump. 'Imported' does not mean 'called': confirm against the gate-title inventories (portps5-zadg, portps5-3eh1) before porting. Licences: AnyPS5 and KytyPS5 GPL-2.0, shadPS4 GPL-2.0-or-later (port with the file's copyright header and cite the commit); SharpEmu is C#, so it is a behavioural reference only.

Blocked by: none.

## Higher Goal

Titles that submit several command buffers at once, wait on GPU EQ events or register resources run instead of aborting.

## Acceptance Criteria

- [ ] Multi-submit loops over the existing single-submit path in order, with SCE codes for bad arguments
- [ ] EQ events attach to the libkernel equeue
- [ ] Resource registration keeps a host-side owner and resource table (names are numbers, never logged as text); it is also the natural feed for portps5-ey3w debug names
- [ ] GoogleTest per export, plus lavapipe for the submit path

## Out of Scope

Capture and Razor APIs.
