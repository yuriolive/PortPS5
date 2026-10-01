---
# portps5-ch5p
title: 'Videodec2: interlaced AVC reports frameMbsOnlyFlag=1 and progressive crop units'
status: todo
type: bug
priority: normal
created_at: 2026-10-01T20:18:35Z
updated_at: 2026-10-01T20:18:37Z
parent: portps5-7dk3
---

## Context

`sceVideodec2GetPictureInfo` (`libSceVideodec2/Export.cpp:544-552`) hard-codes `frameMbsOnlyFlag = 1` and divides the recovered crop by 2 (progressive 4:2:0 crop units). `H264Decoder` opens a generic FFmpeg H.264 decoder that accepts interlaced (field or MBAFF) streams, for which the vertical crop unit is 4, so an interlaced stream reports a wrong vertical crop (found in PR #92 review). Blocked by: none.

## Higher Goal

Picture info matches the SPS for every stream the decoder accepts, or the stream is rejected with a logged error.

## Acceptance Criteria

- [ ] Derive `frameMbsOnlyFlag` and the crop units (CropUnitX/Y from chroma format and frame_mbs_only_flag) from the decoded stream, or reject interlaced streams with a logged error and an SCE error code
- [ ] GoogleTest with a synthetic interlaced stream (or the rejection path)
- [ ] video-fmv.md Videodec2 table updated

## Out of Scope

HEVC.

## Summary of Changes

TBD
