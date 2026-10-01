---
# portps5-06xr
title: 'libSceAgc: port the 82 command-buffer builders that have a reference'
status: todo
type: feature
created_at: 2026-10-01T21:42:53Z
updated_at: 2026-10-01T21:42:53Z
parent: portps5-dbpx
---

## Context

122 libSceAgc exports imported by the dump are stubbed (112) or missing (10). 82 have a reference implementation. Port from AnyPS5 (52): `sceAgcAcbAtomicMem`, `sceAgcAcbCondExec`, `sceAgcAcbCopyData`, `sceAgcAcbDispatchIndirect`, `sceAgcAcbJump`, `sceAgcAcbMemSemaphore`, `sceAgcAcbPopMarker`, `sceAgcAcbPrimeUtcl2`, `sceAgcAcbPushMarker`, `sceAgcAcbResetQueue`, `sceAgcAcbRewind`, `sceAgcAcbSetMarker`, `sceAgcAcb_gQkqkLttcpw`, `sceAgcAsyncRewindPatchSetRewindState`, `sceAgcCreateInterpolantMappingVsPs`, `sceAgcDcbA_zARR5aCmkoY`, `sceAgcDcbAtomicMem`, `sceAgcDcbCondExec`, `sceAgcDcbCopyData`, `sceAgcDcbDispatchIndirect`, `sceAgcDcbDrawIndex`, `sceAgcDcbDrawIndexIndirect`, `sceAgcDcbDrawIndexIndirectMulti`, `sceAgcDcbDrawIndexMultiInstanced`, `sceAgcDcbJump`, `sceAgcDcbMemSemaphore`, `sceAgcDcbPopMarker`, `sceAgcDcbPushMarker`, `sceAgcDcbRewind`, `sceAgcDcbSetBaseIndirectArgs`, `sceAgcDcbSetCxRegisterDirect`, `sceAgcDcbSetCxRegistersIndirectGetSize`, `sceAgcDcbSetIndexCount`, `sceAgcDcbSetMarker`, `sceAgcDcbSetPredication`, `sceAgcDcbSetShRegisterDirect`, `sceAgcDcbSetUcRegisterDirect`, `sceAgcDcbSetUcRegistersIndirectGetSize`, `sceAgcDcbWaitUntilSafeForRendering`, `sceAgcQueueEndOfPipeActionPatchData`, `sceAgcQueueEndOfPipeActionPatchGcrCntl`, `sceAgcQueueEndOfPipeActionPatchType`, `sceAgcRewindPatchSetRewindState`, `sceAgcSetCxRegIndirectPatchSetNumRegisters`, `sceAgcSetShRegIndirectPatchSetNumRegisters`, `sceAgcSetUcRegIndirectPatchSetNumRegisters`, `sceAgcUnknownFuseShaderHalves`, `sceAgcUnknownGetFusedShaderSize`, `sceAgcUnknown_7Wa3aeJgeVU`, `sceAgcUnknown_Ikfdt_MrIqCE`, `sceAgcUnknown_rP5xLdOf26k`, `sceAgcWaitRegMemPatchCompareFunction`. Port from KytyPS5 (17): `sceAgcAcbAcquireMemGetSize`, `sceAgcAcbCondExecGetSize`, `sceAgcAcbCopyDataGetSize`, `sceAgcAcbWaitOnAddressGetSize`, `sceAgcAsyncCondExecPatchSetCommandAddress`, `sceAgcAsyncCondExecPatchSetEnd`, `sceAgcCondExecPatchSetCommandAddress`, `sceAgcCondExecPatchSetEnd`, `sceAgcDcbContextStateOpGetSize`, `sceAgcDcbCopyDataGetSize`, `sceAgcDcbSetWorkloadComplete`, `sceAgcDcbSetWorkloadsActive`, `sceAgcDcbWaitOnAddressGetSize`, `sceAgcGetDataPacketPayloadRange`, `sceAgcGetPacketSize`, `sceAgcSetPacketPredication`, `sceAgcSetRangePredication`. Follow SharpEmu's behaviour (13): `sceAgcAcbDmaDataGetSize`, `sceAgcAcbJumpGetSize`, `sceAgcCbCondWrite`, `sceAgcCbCondWriteGetSize`, `sceAgcCbNopGetSize`, `sceAgcCbSetShRegisterRangeDirectGetSize`, `sceAgcDcbAcquireMemGetSize`, `sceAgcDcbDmaDataGetSize`, `sceAgcDcbRewindGetSize`, `sceAgcDcbSetIndexCountGetSize`, `sceAgcDcbSetShRegisterDirectGetSize`, `sceAgcDcbSetUcRegisterDirectGetSize`, `sceAgcDcbStallCommandBufferParserGetSize`.

Source: NID gap analysis of a local, non-gate dump (PPSA04489), matched against built PortPS5 prx export tables at main@5dd65fe3 and the reference trees AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43 and shadPS4 fecfbed0. Only NIDs and function names were read from the dump. 'Imported' does not mean 'called': confirm against the gate-title inventories (portps5-zadg, portps5-3eh1) before porting. Licences: AnyPS5 and KytyPS5 GPL-2.0, shadPS4 GPL-2.0-or-later (port with the file's copyright header and cite the commit); SharpEmu is C#, so it is a behavioural reference only.

Blocked by: none.

## Higher Goal

Guest code can build ACB and DCB command buffers through the whole libSceAgc builder surface, so the driver sees real PM4 instead of an abort in the builder.

## Acceptance Criteria

- [ ] Each builder emits the same PM4 dwords as its reference; GoogleTest compares the emitted words per builder
- [ ] Its GetSize twin returns the dword count that builder emits, tested against the builder's output
- [ ] Packets the driver cannot execute still abort in the driver (Unsupported), not in the builder
- [ ] Cite the AnyPS5 or KytyPS5 commit in each port commit

## Out of Scope

Driver support for the packets (gpu-driver beans, e.g. portps5-7zu6 for COND_EXEC).
