---
# portps5-de24
title: 'M2: Input TOML bindings and XInput/DualSense matrix'
status: todo
type: feature
priority: high
created_at: 2026-09-30T23:49:49Z
updated_at: 2026-09-30T23:49:49Z
---

## Context

SDL game controllers are wired into scePadRead (PRs #54 and #57; SDL_JOYSTICK and SDL_HIDAPI are ON in CMakeLists.txt:46-48), but only [input] deadzone is consumed. There is no TOML keyboard/mouse binding table, no XInput-specific path, no DualSense USB verification on a physical device, and slot reassignment across plug and unplug is untested with injected SDL events. Spec: docs/spec/input.md.

## Higher Goal

PRD F5: XInput, DualSense over USB and keyboard/mouse mapping, configured in TOML and proven on Dreaming Sarah and TMNT (ROADMAP M2).

## Acceptance Criteria

- [ ] [input.bindings] parsed into the mapping table with rejection of invalid identifiers, with GoogleTest
- [ ] Slot assignment and reassignment tests via synthetic SDL event injection
- [ ] ignore_host_input attach path covered by a test
- [ ] XInput pad and DualSense USB recorded in the manual matrix in the results JSON or release notes
- [ ] input.md M2 row and ROADMAP M2 input item ticked

## Out of Scope

DualSense adaptive triggers, touchpad finger tracking, rumble and lightbar forwarding (post-1.0), libSceMouse exports (separate bean).

## Summary of Changes

TBD
