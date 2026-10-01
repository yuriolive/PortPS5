---
# portps5-ojy1
title: 'Videodec2: colour description flag missing for BT.709-only streams'
status: todo
type: bug
priority: normal
created_at: 2026-10-01T18:53:42Z
updated_at: 2026-10-01T18:53:42Z
parent: portps5-7dk3
---

## Context

libSceVideodec2/Export.cpp:561 sets colourDescriptionPresentFlag only when a colour value is above 2, so a stream with value 1 (BT.709) reports no description (found in the 2026-10-01 spec audit). Blocked by: none.

## Higher Goal

Picture info reports the stream's colour description correctly (video-fmv.md).

## Acceptance Criteria

- [ ] Flag set when any of primaries/transfer/matrix is specified (not 0 or 2)
- [ ] GoogleTest with a synthetic BT.709 stream

## Out of Scope

Other picture-info fields.

## Summary of Changes

TBD
