---
# portps5-j7ds
title: 'M2: DualSense output, motion and hot-plug slot table'
status: completed
type: feature
priority: high
created_at: 2026-10-01T09:00:00Z
updated_at: 2026-10-01T12:00:00Z
---

## Context

SDL HIDAPI was already ON on `main`, but guest output (`scePadSetVibration`, `SetLightBar`, `SetTriggerEffect`, motion enable) was only recorded in `PadManager` and never reached the host controller. Several exports (`scePadResetOrientation`, `GetTriggerEffectState`, `DeviceClass*`) aborted. Motion and touchpad fingers were a constant rest pose. Slot assignment was an inline loop with no same-device reclaim. Spec: docs/spec/input.md.

## Higher Goal

PRD F5 / ROADMAP M2: DualSense over USB reaches the controller with rumble, light bar, motion and touchpad, with the policy and byte maths proven by device-free GoogleTests.

## Acceptance Criteria

- [x] Per-slot output queue (vibration, light bar, trigger effect, motion enable) with a sequence number, reset on `scePadClose`, consumed by the window thread through `PadFetchOutput_nid_postfix`
- [x] Pure encoders for rumble scaling and the six ScePadTriggerEffectModes, plus guest param parsing that rejects null, short and malformed buffers (`PadOutputMapping.hpp`)
- [x] Pure motion maths: m/s^2 to g, Mahony fusion with NaN and step guards, touchpad scaling, touch ids (`PadMotion.hpp`)
- [x] Pure `SlotTable`: lowest free slot, same-GUID reclaim, duplicate and overflow handling, wired into `PadInput`
- [x] `scePadResetOrientation`, `GetTriggerEffectState`, `DeviceClassGetExtendedInformation`, `DeviceClassParseData`, `SetVibrationMode`, `SetTriggerEffect` return SCE codes instead of aborting or ignoring arguments
- [x] SDL HIDAPI build guard test and licence recorded in docs/spec/build-toolchain.md
- [x] GoogleTests for all of the above, with failing-first proof

## Out of Scope

Audio mixer changes (DualSense speaker and actuator audio, upstream dee927ea; bean `portps5-ds7a`), Bluetooth DualSense, per-title mappings, recorded input, physical-device verification (bean `portps5-ds7h`), TOML bindings (bean `portps5-de24`).

## Summary of Changes

- `core/libs/prx/libScePad/include/PadOutputMapping.hpp`, `PadMotion.hpp`, `PadSlotTable.hpp` (new, pure, header-only).
- `PadState.hpp`, `PadInternal.hpp`, `PadState.cpp`: per-slot `PadOutputState`, motion fusion, touch ids, `FetchOutput`, `CheckOpenHandle`, reset on close.
- `Export.cpp`: real implementations and doc comments for the output and device-class exports.
- `libSceVideoOut` `PadInput`: `SlotTable` hot-plug, sensor enable, touchpad and motion sampling, `applyOutput` to SDL rumble, LED and DS5 trigger effects.
- `tests/input/Pad*Tests.cpp` and `tests/CMakeLists.txt`; `tests/expected-count` 302 to 342.
- Specs: docs/spec/input.md, docs/spec/build-toolchain.md, docs/ROADMAP.md.
