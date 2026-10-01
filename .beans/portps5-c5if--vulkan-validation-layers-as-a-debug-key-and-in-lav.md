---
# portps5-c5if
title: Vulkan validation layers as a debug key, and in lavapipe CI
status: todo
type: feature
priority: high
created_at: 2026-10-01T21:35:20Z
updated_at: 2026-10-01T21:35:20Z
parent: portps5-etxc
---

## Context

No validation layer is enabled anywhere on PortPS5 main@5dd65fe3, in code or in `.github/workflows/ci.yml`. Barrier and synchronisation errors are the most common Vulkan bug class and are invisible without the synchronisation validation layer. The Khronos layer is Apache-2.0, which is fine as a CI and debug tool that is never linked into a shipped binary (PRD R1).

Blocked by: none.

## Higher Goal

Validation errors are caught in CI on synthetic tests and on demand in local runs.

## Acceptance Criteria

- [ ] `debug.gpu.validation = [core, sync, gpu_assisted, best_practices]` enables the layer and routes messenger output to the log; each error increments a counter reported at run end
- [ ] The driver-lavapipe job installs a pinned, SHA-256-verified layer and runs with `core` and `sync`; any validation error fails the job
- [ ] Known false positives are listed by message ID in a checked-in suppression file, each with a reason
- [ ] verification.md section 1 updated

## Out of Scope

Fixing every existing validation error in this bean (each gets its own bean).
