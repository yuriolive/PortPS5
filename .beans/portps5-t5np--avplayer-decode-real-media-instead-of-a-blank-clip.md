---
# portps5-t5np
title: 'AvPlayer: decode real media instead of a blank clip'
status: todo
type: feature
created_at: 2026-10-01T21:28:54Z
updated_at: 2026-10-01T21:28:54Z
parent: portps5-dbpx
---

## Context

libSceAvPlayer.native/Export.cpp on PortPS5 main@5dd65fe3 runs a state machine over a fixed 4-second blank clip. shadPS4, KytyPS5 and SharpEmu decode real media. The LGPL FFmpeg already pinned for Videodec2 can decode.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

FMV played through AvPlayer shows real frames and audio (PRD F4).

## Acceptance Criteria

- [ ] Container and codec decode through the pinned LGPL FFmpeg
- [ ] A/V sync through the mixer and video_latency_ms
- [ ] GoogleTest with a synthetic clip; video-fmv.md updated

## Out of Scope

Bink.
