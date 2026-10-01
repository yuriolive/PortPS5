---
# portps5-7mnt
title: 'pad: identify controllers by serial, not only GUID, for slot reclaim'
status: todo
type: task
priority: low
created_at: 2026-10-01T19:52:39Z
updated_at: 2026-10-01T19:52:39Z
---

## Context
`SlotTable` (core/libs/prx/libScePad/include/PadSlotTable.hpp) remembers the last `SDL_JoystickGUID` per slot to give a returning pad its old slot. In SDL2 the GUID is built from bus, vendor, product, version and CRC, not the serial number, so two identical pads (two DualSenses, two Xbox pads) share one GUID. If both drop out and the second reconnects first, it takes the first free slot with a matching GUID, which can be the other pad's slot. Raised in review on #81 (Gitar). Real but low severity: it needs two identical pads disconnecting together.

## Higher Goal
Stable player-to-slot assignment across hot-plug (PRD F5).

## Acceptance Criteria
- [ ] Identity prefers the device serial (`SDL_GameControllerGetSerial` / `SDL_JoystickGetSerial`, SDL >= 2.0.14) when present, falling back to the GUID.
- [ ] SlotTable unit test with two pads of the same GUID and different serials reconnecting in swapped order keeps each on its own slot.
- [ ] Behaviour with no serial (keyboard virtual pad, some Bluetooth stacks) is unchanged.

## Out of Scope
Persisting slot assignment across process restarts; per-title mappings.

## Summary of Changes
Open. Found during review of PR #81.
