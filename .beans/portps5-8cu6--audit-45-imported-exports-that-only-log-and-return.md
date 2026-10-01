---
# portps5-8cu6
title: Audit 45 imported exports that only log and return a constant
status: todo
type: task
created_at: 2026-10-01T22:10:42Z
updated_at: 2026-10-01T22:10:42Z
parent: portps5-dbpx
---

## Context

The NID gap analysis (local non-gate dump PPSA04489, PortPS5 main@5dd65fe3) found 45 imported exports whose body only logs and returns a constant. Many are legitimate no-ops (init/terminate), but some may be disguised stubs. Names marked * have a non-trivial implementation in at least one reference (AnyPS5, KytyPS5, shadPS4, or SharpEmu as behaviour only). Priority suspects: `__error` (must return the per-thread errno address), `sceKernelGetDirectMemorySize`, `pthread_equal`, and the libSceAgc GetSize exports (must return the builder's dword count; see portps5-9zm1).

- libSceAgc: `sceAgcCbBranchGetSize*` `sceAgcCbQueueEndOfPipeActionGetSize*` `sceAgcDcbDispatchIndirectGetSize` `sceAgcDcbDrawIndexAutoGetSize` `sceAgcDcbDrawIndexGetSize` `sceAgcDcbDrawIndexIndirectGetSize*` `sceAgcDcbDrawIndexIndirectMultiGetSize*` `sceAgcDcbDrawIndirectGetSize` `sceAgcDcbDrawIndirectMultiGetSize` `sceAgcDcbGetLodStatsGetSize*` `sceAgcDcbJumpGetSize*` `sceAgcDcbSetBaseDispatchIndirectArgsGetSize` `sceAgcDcbSetBaseDrawIndirectArgsGetSize` `sceAgcDcbSetNumInstancesGetSize`
- libSceNpUniversalDataSystem: `sceNpUniversalDataSystemDestroyContext*` `sceNpUniversalDataSystemDestroyHandle*` `sceNpUniversalDataSystemPostEvent*` `sceNpUniversalDataSystemRegisterContext*` `sceNpUniversalDataSystemTerminate*`
- libSceSystemService: `sceSystemServiceHideSplashScreen*`
- libSceSaveData_native: `sceSaveDataDeleteTransactionResource*` `sceSaveDataPrepare*`
- libSceUserService: `sceUserServiceInitialize*` `sceUserServiceTerminate*`
- libkernel: `__error*` `sceKernelGetDirectMemorySize*`
- libSceNpManager: `sceNpCheckCallback*` `sceNpCheckPremium*` `sceNpDeleteRequest*` `sceNpNotifyPremiumFeature`
- libSceNpGameIntent: `sceNpGameIntentInitialize*`
- libScePosix: `pthread_equal*`
- libSceNpCommerce: `sceNpCommerceDialogUpdateStatus*`
- libSceNpWebApi2: `sceNpWebApi2AddHttpRequestHeader*` `sceNpWebApi2DeleteRequest*` `sceNpWebApi2DeleteUserContext*` `sceNpWebApi2PushEventCreatePushContext*` `sceNpWebApi2PushEventDeleteFilter*` `sceNpWebApi2PushEventDeleteHandle*` `sceNpWebApi2PushEventDeletePushContext*` `sceNpWebApi2PushEventStartPushContextCallback*` `sceNpWebApi2PushEventUnregisterPushContextCallback*` `sceNpWebApi2ReadData*` `sceNpWebApi2SendRequest*`
- libSceNpSessionSignaling: `sceNpSessionSignalingInitialize*`

Blocked by: none.

## Higher Goal

No export silently returns a wrong value that a title later trips over.

## Acceptance Criteria

- [ ] Each export classified: correct no-op (comment says why), or fixed
- [ ] `__error`, `sceKernelGetDirectMemorySize` and `pthread_equal` verified against the reference behaviour with GoogleTest
- [ ] GetSize exports covered by the packet-length table in portps5-9zm1
- [ ] Offline NP/WebApi2 returns use the offline codes from portps5-p5yj

## Out of Scope

Exports not imported by any title we test.

## Summary of Changes

TBD
