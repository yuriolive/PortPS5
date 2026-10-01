---
# portps5-k7q2
title: 'M2: AudioOut2 16-bit PCM ports and 64-bit Ngs2WaveformBlock layout'
status: completed
type: task
priority: normal
created_at: 2026-10-01T04:00:00Z
updated_at: 2026-10-01T04:00:00Z
---

## Context

AudioOut2 ports read every PCM buffer as 32-bit float. A port opened with the signed 16-bit sample type (`data_format` low bits = 1, for example 0x201) was read at twice its size. `Ngs2WaveformBlock` used the 32-byte PS4 layout while the PS5 record is 40 bytes (64-bit offset and size).

## Higher Goal

Audio ports render what the title sent, through the single host mixer (PR #45), and guest-visible structs match the PS5 layout.

## Acceptance Criteria

- [x] `data_format` bits 0..6 decoded; float and signed 16-bit rendered, other types left unrendered
- [x] 16-bit mono, stereo and 7.1 bed mix like the float path
- [x] `Ngs2WaveformBlock` is 40 bytes with `static_assert`
- [x] GoogleTest coverage, proven to fail without the change
- [x] `docs/spec/audio.md` updated

## Out of Scope

Ambisonic speaker arrays (no public oracle for the encoding), AJM MP3 (needs the pinned FFmpeg build), NGS2 runtime.

## Summary of Changes

`AudioOut2Internal.hpp` (sample type, `AudioOut2LoadFrame`), `AudioOut2Port.cpp`, `SceTypes.hpp`, `core/libs/tests/AudioOut2.cpp`, `docs/spec/audio.md`.
