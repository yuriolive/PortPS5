---
# portps5-h2rv
title: Optional on-screen debug overlay
status: todo
type: feature
created_at: 2026-10-01T21:35:20Z
updated_at: 2026-10-01T21:35:20Z
parent: portps5-etxc
blocked_by:
    - portps5-71o2
    - portps5-hfiw
---

## Context

Counters and the frame breakdown are only readable after a run. An overlay (Dear ImGui, MIT) drawn in the presenter's final pass shows them live.

Blocked by: portps5-71o2 (presenter composition hook), portps5-hfiw (frame breakdown).

## Higher Goal

Live frame time, breakdown and invariant counters during local investigation.

## Acceptance Criteria

- [ ] `debug.overlay` draws after guest composition and never into a guest image
- [ ] Off by default; setting it is a `[debug]` key, so such a run never passes (verification.md 4.2)
- [ ] Dear ImGui pinned under 3rdparty/ with its licence

## Out of Scope

Interactive debug menus that change guest state.
