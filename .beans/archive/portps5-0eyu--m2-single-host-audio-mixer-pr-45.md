---
# portps5-0eyu
title: 'M2: Single host audio mixer (PR #45)'
status: completed
type: feature
priority: high
created_at: 2026-09-30T23:46:51Z
updated_at: 2026-10-01T00:39:50Z
---


## Context

AudioOut v1 ports and AudioOut2 contexts each open their own SDL stream on independent device clocks, with a fixed 0.5 master gain and LFE dropped from 7.1 folds (docs/spec/audio.md Current state). PR #45 (merged 2026-09-30) adds one process-wide AudioMixer with SPSC rings, resampling, soft limiter, underrun counters and GTest suites.

## Higher Goal

PRD F3: one device clock, at most 1 underrun per 10 minutes, measured by telemetry (ROADMAP M2).

## Acceptance Criteria

- [x] PR #45 reviewed, rebased on main and merged
- [x] Driverless mixer test shows 0 underruns over 10 simulated minutes and exactly N counted for N injected gaps
- [x] audio.md M2 row and ROADMAP M2 audio items ticked
- [x] AudioOut2 legacy runner tests (core/libs/tests/AudioOut2.cpp still has a manual main()) converted to GoogleTest

## Out of Scope

AJM codecs beyond ATRAC9, NGS2/Audio3d/Audiodec runtime, object-port panning (M4/M5).

## Summary of Changes

PR #45: `libSceAudioOut/src/AudioMixer.{hpp,cpp}` single mixer (SPSC rings, resampler, soft limiter, underrun and overrun counters), AudioOut v1 and AudioOut2 routed through it, `AudioMixerTests`, `AudioOut2KytyPorts`, AudioOut2 and AudioAjm tests converted to GoogleTest, `docs/spec/audio.md`, `docs/ROADMAP.md`.
