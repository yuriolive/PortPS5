---
# portps5-bbxe
title: RenderDoc in-app capture trigger
status: todo
type: feature
created_at: 2026-10-01T21:35:19Z
updated_at: 2026-10-01T21:35:19Z
parent: portps5-etxc
blocked_by:
    - portps5-ey3w
---

## Context

RenderDoc's in-application API (renderdoc_app.h, MIT) can start and end a capture around chosen frames when renderdoc.dll is already loaded, without a manual hotkey. Every capture, dump and report stays in the install directory on the maintainer's machine; only numeric fields may reach the results JSON (.agents/rules/legal-boundary.md).

Blocked by: portps5-ey3w (labels make captures readable).

## Higher Goal

A wrong frame can be captured by frame number from config, repeatably.

## Acceptance Criteria

- [ ] `debug.gpu.capture = {frame, count}` triggers captures through the API only when RenderDoc is injected; otherwise one warning
- [ ] renderdoc_app.h vendored under 3rdparty/ with its MIT licence; nothing linked
- [ ] Captures written to `<install>/dumps/captures`

## Out of Scope

Bundling RenderDoc.
