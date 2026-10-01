---
# portps5-636k
title: 'GPU crash diagnostics: VK_EXT_device_fault and command-stream breadcrumbs'
status: todo
type: feature
priority: high
created_at: 2026-10-01T21:35:19Z
updated_at: 2026-10-01T21:35:19Z
parent: portps5-etxc
blocked_by:
    - portps5-tiod
---

## Context

On VK_ERROR_DEVICE_LOST the driver logs the last recorded serials and PacketHistory, which say what was submitted, not where the GPU stopped. VK_EXT_device_fault reports the faulting address and vendor fault info; breadcrumb markers (VK_AMD_buffer_marker, VK_NV_device_diagnostic_checkpoints) written before and after each draw and dispatch show the last completed one. Every capture, dump and report stays in the install directory on the maintainer's machine; only numeric fields may reach the results JSON (.agents/rules/legal-boundary.md).

Blocked by: portps5-tiod (Recorder in the submit path).

## Higher Goal

A device loss names the guest packet, draw index and pipeline that was executing.

## Acceptance Criteria

- [ ] AMD path: with VK_AMD_buffer_marker, `debug.gpu.breadcrumbs` writes a marker per draw and dispatch into a host-visible buffer (vkCmdWriteBufferMarkerAMD); off by default, since markers cost GPU time
- [ ] NV path: with VK_NV_device_diagnostic_checkpoints, it inserts checkpoints (vkCmdSetCheckpointNV) and reads them back after the loss with vkGetQueueCheckpointDataNV; no buffer
- [ ] On device loss: VK_EXT_device_fault info (when supported) plus the last completed marker mapped back to the PM4 packet and draw index, written to the log before the abort
- [ ] Unit test (no device): mapping from a marker or checkpoint value to PM4 packet and draw index on a synthetic stream
- [ ] Marker emission tested against a mocked Vulkan dispatch (GMock) that records vkCmdWriteBufferMarkerAMD and vkCmdSetCheckpointNV calls, since stock lavapipe exposes neither extension; a real AMD or NV driver run is local only
- [ ] gpu-driver.md Failure modes row updated

## Out of Scope

Vendor SDKs (Nsight Aftermath, Radeon GPU Detective) as linked dependencies: they are proprietary. Using them as external tools on local runs needs no code here.
