---
# portps5-etxc
title: 'Diagnostics track: GPU crash triage, captures, validation, symbols'
status: todo
type: epic
created_at: 2026-10-01T21:35:19Z
updated_at: 2026-10-01T21:35:19Z
---

## Context

Telemetry and performance measurement are planned in the performance track, but failure triage is thin on PortPS5 main@5dd65fe3: a VK_ERROR_DEVICE_LOST logs the packet history and aborts (gpu-driver.md Failure modes), no Vulkan validation layer is used in code or CI, Vulkan objects are unnamed so RenderDoc and RGP captures are anonymous, there is no shader printf, crash reports carry raw addresses, and the `asan` preset cannot link with MinGW GCC. Every capture, dump and report stays in the install directory on the maintainer's machine; only numeric fields may reach the results JSON (.agents/rules/legal-boundary.md).

Blocked by: none.

## Higher Goal

A failure on a gate title is diagnosable from local artifacts in one run instead of by bisection.

## Acceptance Criteria

- [ ] Child beans completed or re-planned with data
- [ ] ROADMAP Diagnostics track checkboxes ticked

## Out of Scope

Telemetry fields and profiling (performance track, epic portps5-7fqk).
