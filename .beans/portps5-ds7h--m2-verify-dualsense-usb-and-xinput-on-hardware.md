---
# portps5-ds7h
title: 'M2: Verify DualSense USB output, motion and XInput on hardware'
status: todo
type: task
priority: high
created_at: 2026-10-01T12:00:00Z
updated_at: 2026-10-01T12:00:00Z
---

## Context

PR for bean `portps5-j7ds` implements and unit-tests the DualSense output path (rumble, light bar, trigger effects, motion, touchpad, slot reclaim) with synthetic data only. CI has no controller, so nothing about real HID behaviour is verified: SDL HIDAPI driver selection on Windows, USB enhanced reports, DS5 trigger effect bytes, sensor axis signs, light bar default colour, hot-plug timing.

## Higher Goal

PRD F5 and the ROADMAP M2 input exit item: XInput, DualSense USB and keyboard/mouse pass the input matrix on Dreaming Sarah and TMNT.

## Acceptance Criteria

- [ ] DualSense over USB: rumble, light bar set/reset, motion axes and signs, touchpad fingers checked by the maintainer and recorded in the results JSON (`compat-result` skill)
- [ ] Adaptive trigger encodings (post-1.0, best effort) spot-checked or marked unsupported in docs/spec/input.md
- [ ] XInput pad: buttons, sticks, triggers, rumble, hot-plug
- [ ] Unplug and replug returns the pad to the same slot
- [ ] docs/spec/input.md manual matrix and ROADMAP M2 input item ticked

## Out of Scope

Bluetooth DualSense, speaker and actuator audio (bean `portps5-ds7a`).

## Summary of Changes

TBD
