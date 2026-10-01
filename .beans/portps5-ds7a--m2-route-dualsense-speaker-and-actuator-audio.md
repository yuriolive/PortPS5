---
# portps5-ds7a
title: 'M2: Route DualSense speaker and actuator audio to the controller'
status: todo
type: feature
priority: low
created_at: 2026-10-01T12:00:00Z
updated_at: 2026-10-01T12:00:00Z
---

## Context

AudioOut2 port types 0x3 (controller speaker) and 0x6 (two voice-coil actuators) are folded into the TV mix. AnyPS5 `main@dee927ea` plays them on the USB DualSense sound card (channels 1-2 speaker, 3-4 actuators). It changes `AudioOut2Context.cpp`/`AudioOut2Port.cpp` heavily, opens an SDL audio device, depends on the host sound server channel labelling (PulseAudio quad quirk) and uses `throw std::invalid_argument` (banned across the ABI boundary). It was reviewed in the input lane and skipped as audio-mixer scope.

## Higher Goal

Titles that drive controller haptics through audio ports feel correct on a DualSense, without disturbing the single host mixer (PR #45).

## Acceptance Criteria

- [ ] Re-review `dee927ea` against the merged single mixer; port the pure helpers (`AudioOut2PadMix`) with GoogleTest and no throws
- [ ] Device discovery and unplug handling behind the existing audio device abstraction
- [ ] Verified locally on a USB DualSense by the maintainer (cannot run in CI)
- [ ] docs/spec/audio.md updated

## Out of Scope

Bluetooth, WASAPI exclusive mode, haptics synthesis.

## Summary of Changes

TBD
