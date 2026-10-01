---
# portps5-tiod
title: 'M1: Wire Recorder and HostImport into sceAgcDriverSubmitDcb'
status: todo
type: feature
priority: high
created_at: 2026-09-30T23:49:21Z
updated_at: 2026-10-01T17:59:27Z
parent: portps5-4ut1
---


## Context

PR #49 landed the adapted Recorder and HostImport (Graphics/src/Recorder.cpp, HostImport.cpp) with 35 GoogleTests, but nothing in the runtime constructs them: VulkanDevice::Dispatch/Draw still execute synchronously and sceAgcDriverSubmitDcb (Submit/src/Dcb.cpp:31) does not record through the Recorder. Spec: docs/spec/gpu-driver.md (Landed in PortPS5, open question 7).

## Higher Goal

Serial-based submission with pending-write tracking, label late-trust and host import, as the capture-ordering redesign in gpu-driver.md requires (ROADMAP M1 driver port).

## Acceptance Criteria

- [ ] Dispatch, Draw and sceAgcDriverSubmitDcb record into Recorder and bind guest memory through HostImport
- [ ] matchesFillKernel, matchesCopyKernel, tolerate-skips and the failure memo are not present in the ported driver
- [ ] No leaks or races at teardown in the real submit path, covered by a lavapipe GoogleTest
- [ ] gpu-driver.md M1 row and ROADMAP M1 Recorder and host-import items updated

## Out of Scope

GPU-side indirect draws (M3), automatic host-import budget (M5), module split (M3).

## Summary of Changes

TBD
