---
# portps5-v2dc
title: 'M1: libSceVideodec2 AVC decode through a pinned LGPL FFmpeg'
status: completed
type: feature
priority: normal
created_at: 2026-10-01T17:00:00Z
updated_at: 2026-10-01T17:00:00Z
---

## Context

`libSceVideodec2` did not exist in this tree. Upstream AnyPS5 ported it (`a38ef613`) with error codes and an `AvcPictureInfo` layout that disagree with the public oracles, returned black pictures without FFmpeg, and later moved to a prebuilt `ffmpeg-core` download whose configure flags and licence cannot be verified. PortPS5 is GPL-2.0-only, so any FFmpeg must be a plain LGPL-2.1+ build.

## Higher Goal

Titles that feed H.264 access units to the system video decoder get real pictures (ROADMAP M1 codec items, PRD F4 FMV), with a build-time and run-time guarantee that the linked FFmpeg stays LGPL-2.1+.

## Acceptance Criteria

- [x] FFmpeg pinned as a shallow submodule (n9.0.2, commit recorded) and built by CMake with the documented LGPL-only configure
- [x] Licence gate: post-configure script, fixture ctest cases, run-time licence assertion
- [x] `libSceVideodec2` AVC exports with oracle layouts, error codes and NV12 layout, no fabricated pictures, no throws
- [x] Synthetic-stream GoogleTest suites; mutation-checked
- [x] `docs/spec/build-toolchain.md`, `docs/spec/video-fmv.md`, `docs/ROADMAP.md` updated

## Out of Scope

HEVC and interlaced second pictures, AJM MP3 (follow-up on this FFmpeg), AvPlayer decoding (Media Foundation decision unchanged), playback quality.

## Summary of Changes

`3rdparty/FFmpeg` + `.gitmodules`, `cmake/PortPS5FFmpeg.cmake`, `cmake/PortPS5FFmpegLicenseCheck.cmake`, `CMakePresets.json` (ci requires FFmpeg), `core/libs/GuestRangeCheck.hpp`, `core/libs/prx/libSceVideodec2/*`, `tests/video/*`, `tests/ffmpeg/fixtures/*`, `tests/CMakeLists.txt`, specs and ROADMAP.
