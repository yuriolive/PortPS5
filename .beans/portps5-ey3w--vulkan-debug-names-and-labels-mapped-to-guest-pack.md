---
# portps5-ey3w
title: Vulkan debug names and labels mapped to guest packets
status: todo
type: feature
created_at: 2026-10-01T21:35:19Z
updated_at: 2026-10-01T21:35:19Z
parent: portps5-etxc
---

## Context

Driver objects carry no VK_EXT_debug_utils names and command buffers no labels, so a RenderDoc, RGP or Nsight capture shows anonymous images and pipelines. Every capture, dump and report stays in the install directory on the maintainer's machine; only numeric fields may reach the results JSON (.agents/rules/legal-boundary.md).

Blocked by: none.

## Higher Goal

External GPU tools show guest-meaningful names: queue, PM4 packet index, draw index, shader stage and hash, render-target address.

## Acceptance Criteria

- [ ] `debug.gpu.labels` enables object names and begin/end labels per submit, draw and dispatch
- [ ] Names use numbers and hashes only, never guest strings
- [ ] Zero cost when off: the function pointers are not loaded
- [ ] lavapipe test with a debug messenger checks the labels

## Out of Scope

Capture triggering (separate bean).
