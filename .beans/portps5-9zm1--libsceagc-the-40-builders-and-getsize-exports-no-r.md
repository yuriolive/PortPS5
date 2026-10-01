---
# portps5-9zm1
title: 'libSceAgc: the 40 builders and GetSize exports no reference implements'
status: todo
type: task
created_at: 2026-10-01T21:42:53Z
updated_at: 2026-10-01T21:42:53Z
parent: portps5-dbpx
blocked_by:
    - portps5-06xr
---

## Context

No reference implements these. GetSize exports (29): `sceAgcAcbAtomicGdsGetSize`, `sceAgcAcbAtomicMemGetSize`, `sceAgcAcbDispatchIndirectGetSize`, `sceAgcAcbEventWriteGetSize`, `sceAgcAcbPrimeUtcl2GetSize`, `sceAgcAcbQueueEndOfShaderActionGetSize`, `sceAgcAcbRewindGetSize`, `sceAgcCbDispatchGetSize`, `sceAgcCbSetShRegistersDirectGetSize`, `sceAgcCbSetUcRegisterRangeDirectGetSize`, `sceAgcCbSetUcRegistersDirectGetSize`, `sceAgcDcbAtomicGdsGetSize`, `sceAgcDcbAtomicMemGetSize`, `sceAgcDcbBeginOcclusionQueryGetSize`, `sceAgcDcbCondExecGetSize`, `sceAgcDcbDrawIndexMultiInstancedGetSize`, `sceAgcDcbDrawIndexOffsetGetSize`, `sceAgcDcbEndOcclusionQueryGetSize`, `sceAgcDcbEventWriteGetSize`, `sceAgcDcbPrimeUtcl2GetSize`, `sceAgcDcbQueueEndOfShaderActionGetSize`, `sceAgcDcbSetBoolPredicationEnableGetSize`, `sceAgcDcbSetCxRegisterDirectGetSize`, `sceAgcDcbSetIndexBufferGetSize`, `sceAgcDcbSetIndexIndirectArgsGetSize`, `sceAgcDcbSetIndexSizeGetSize`, `sceAgcDcbSetPredicationDisableGetSize`, `sceAgcDcbSetZPassPredicationEnableGetSize`, `sceAgcDcbWriteDataGetSize`. Each returns the packet length of its builder, so it can be derived from the builder. Others (11): `sceAgcAcbSetFlip`, `sceAgcAcbSetWorkloadComplete`, `sceAgcAcbSetWorkloadStreamInactive`, `sceAgcAcbSetWorkloadsActive`, `sceAgcAcbWaitUntilSafeForRendering`, `sceAgcBranchPatchSetCompareAddress`, `sceAgcDcbPrimeUtcl2`, `sceAgcDcbSetIndexIndirectArgs`, `sceAgcDcbSetWorkloadStreamInactive`, `sceAgcDebugRaiseException`, `sceAgcGetDefaultCxStateFlat`.

Source: NID gap analysis of a local, non-gate dump (PPSA04489), matched against built PortPS5 prx export tables at main@5dd65fe3 and the reference trees AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43 and shadPS4 fecfbed0. Only NIDs and function names were read from the dump. 'Imported' does not mean 'called': confirm against the gate-title inventories (portps5-zadg, portps5-3eh1) before porting. Licences: AnyPS5 and KytyPS5 GPL-2.0, shadPS4 GPL-2.0-or-later (port with the file's copyright header and cite the commit); SharpEmu is C#, so it is a behavioural reference only.

Blocked by: portps5-06xr.

## Higher Goal

One packet-length table drives both builders and GetSize exports, so they cannot disagree.

## Acceptance Criteria

- [ ] A packet-length table shared by builders and GetSize exports; a test checks every GetSize against its builder
- [ ] The 11 other exports implemented from the PM4 layout (public RDNA2 PM4 docs), or left as Unsupported with a reason in the bean
- [ ] GoogleTest per export

## Out of Scope

Exports not imported by any gate title.
