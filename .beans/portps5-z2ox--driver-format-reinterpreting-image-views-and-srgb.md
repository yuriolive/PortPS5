---
# portps5-z2ox
title: 'Driver: format-reinterpreting image views and sRGB sampling'
status: todo
type: feature
created_at: 2026-10-01T21:28:54Z
updated_at: 2026-10-01T21:28:54Z
parent: portps5-oo21
---

## Context

A sampler that forces sRGB aborts (libSceAgcDriver Graphics/src/GuestSamplerResource.cpp:79, PortPS5 main@5dd65fe3), and no image is created with VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT. shadPS4, SharpEmu and KytyPS5 reinterpret formats through views.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

3D titles that sample render targets as sRGB or reinterpret formats draw instead of aborting.

## Acceptance Criteria

- [ ] Images created MUTABLE_FORMAT with a format list when reinterpretation is possible
- [ ] sRGB-forcing samplers map to sRGB views
- [ ] lavapipe tests per reinterpretation class

## Out of Scope

Block-compressed reinterpretation.
