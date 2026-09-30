# PortPS5 — Spec: Input

Status: draft v1 · 2026-09-27

## Scope

Guest controller input through `libScePad`, plus the guest keyboard and mouse libraries (`libSceMouse`, `libSceKeyboard`). The host side covers:
- XInput controllers;
- DualSense over USB;
- keyboard and mouse mapped onto a virtual pad;
- hot-plug and user-to-pad assignment.

Out of scope for 1.0 (PRD §5):
- DualSense haptics and adaptive triggers;
- Bluetooth-specific DualSense features;
- touchpad gestures beyond a click.

PRD bar owned here: F5 (XInput, DualSense USB, keyboard/mouse mapping).

## Current state

References are relative to the AnyPS5 tree. Both trees are identical here unless a difference is noted.

| Area | Verified state |
|---|---|
| Physical controllers | **SDL GameController wired (this PR).** `libSceVideoOut/src/PadInput.cpp` opens controllers on `SDL_CONTROLLERDEVICEADDED` (lowest free slot 0..3), polls them every window-loop tick and publishes via `PadPublishControllerInput_nid_postfix`; `libScePad/include/ControllerMapping.hpp` holds the pure button/axis translation. Historical note on the upstream state: **None.** Neither tree references `SDL_GameController`, `SDL_Joystick` or XInput anywhere under `core/`. The top-level `CMakeLists.txt:26-28` forces `SDL_JOYSTICK`, `SDL_HAPTIC` and `SDL_HIDAPI` off, and `:29` forces `SDL_SENSOR` off. |
| Keyboard/mouse → pad | A compile-time table of 33 bindings (`core/libs/prx/libScePad/include/InputMapping.hpp:17-51`): WASD for the left stick, TFGH for the right stick, mouse-left = Square, mouse-right = R2, wheel = D-pad up/down, middle-click toggles mouse-look, F11 toggles fullscreen. Mouse-look polls every 33 ms at a fixed sensitivity (`:13-15`). |
| Event plumbing | Events are read in the VideoOut window loop (`libSceVideoOut/src/VideoOutDriver.cpp:432-446` in PR #5), then `PadInput::HandleEvent` / `publish` run (`libSceVideoOut/src/PadInput.cpp:23-135`). `APS5_NO_PAD_INPUT` drops keys and buttons (`PadInput.cpp:31-34`). |
| `scePadOpen` | Accepts index 0 only and returns a constant handle (`libScePad/Export.cpp:77-88`). |
| `scePadRead` | `main` throws. PR #5 returns one current state per call (`Export.cpp:92-98`). |
| Pad state | Always reports connected, and `connectedCount` is 1 (`Export.cpp:37-55`; `src/PadState.cpp:35-36`). L2/R2 analog values are synthesised as 0 or 255 from the digital bits (`PadState.cpp:31-32`). Motion is constant (`:33-34`). The timestamp advances only when the state changes (`:47-53`). |
| Output features | `scePadSetVibration`, `SetLightBar`, `ResetLightBar`, `SetTriggerEffect`, `SetTiltCorrectionState`, `ResetOrientation` and `GetHandle` all throw (`Export.cpp:57-170`). `SetMotionSensorState` returns OK. |
| `libSceMouse`, `libSceKeyboard` | Every export throws (`libSceMouse/Export.cpp:8-32`, `libSceKeyboard/Export.cpp:8-50`). |
| Known debt | `docs/TechnicalDebt.md:33`: there is no way to change the keyboard/mouse mapping. |

the decision table in [README.md](README.md#subsystem-specs) says "`libScePad` implements `scePadRead` over SDL". That is accurate only for keyboard and mouse events. XInput and DualSense support is new work, and it needs SDL subsystems that the build currently disables.

## Decision

This follows the decision table in [README.md](README.md#subsystem-specs) §Input:
- Keep SDL and extend it.
- Add a keyboard/mouse mapping and bindings table in TOML.
- Add hot-plug and controller-number assignment.
- DualSense haptics and adaptive triggers are post-1.0.

## Target design

1. **Build.** Turn on `SDL_JOYSTICK` and `SDL_HIDAPI`. Leave `SDL_HAPTIC` and `SDL_SENSOR` off in 1.0. Pin an SDL2 revision whose HIDAPI driver supports PS5 controllers. *Inference:* that arrived in SDL 2.0.14. The pinned submodule commit `4b69833` must be checked against it ([build-toolchain.md](build-toolchain.md)).
2. **Device layer.** An `InputHub` owns SDL `GameController` instances and receives `CONTROLLERDEVICEADDED` / `REMOVED` events from the existing window loop.
   - XInput devices come through SDL's XInput/RawInput backends.
   - DualSense over USB comes through HIDAPI (`SDL_HINT_JOYSTICK_HIDAPI_PS5=1`). Full-report mode is enabled only when a later feature needs it.
3. **Pad slots.** Up to 4 slots map to the logged-in users that `libSceUserService` reports. 1.0 has one user (see [save-data.md](save-data.md)).
   - The first controller that connects takes slot 0.
   - Keyboard/mouse is always an extra source merged into slot 0.
   - A disconnected controller frees its slot. The guest sees `connected = false` until a controller reconnects, and it is then reassigned to the same slot if it is the same device GUID.
4. **State model.** Each slot keeps real analog sticks and triggers, buttons, and `connectedCount` incremented on every reconnect. The timestamp is refreshed on every host poll, not only on change, so titles that detect a stale timestamp see a live pad. Guest dead zones come from `GetControllerInformation`. The host applies no extra dead zone beyond an 8% radial default, which is configurable.
5. **Mapping sources, merged per slot:**
   - Controller: SDL canonical layout to the pad layout (South = Cross, East = Circle, and so on).
   - Keyboard/mouse: the TOML bindings table. The current `InputMapping.hpp` table becomes the shipped default in `config/global.toml`.
6. **TOML schema** (the `[input]` section; validation rules are in [configuration.md](configuration.md)):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `deadzone` | float 0.0–0.5 | 0.08 | Host radial dead zone for sticks. |
| `mouse_look` | bool | false | Start with mouse-look on. The middle button still toggles it. |
| `mouse_sensitivity` | float 0.1–10 | 1.0 | Replaces `Pad::MouseSensitivity`. |
| `swap_confirm` | bool | false | Swap Cross and Circle for controllers only. |
| `bindings` | table `name → [keys]` | the current table | `cross = ["Return", "Space"]`, `left_stick_left = ["A"]`, `r2 = ["MouseRight"]`, `up = ["WheelUp"]`. SDL scancode names are used. An unknown key name is a validation error. |

7. **Output calls.**
   - `SetVibration` forwards to `SDL_GameControllerRumble` when available, and otherwise returns OK. Rumble is basic XInput/HIDAPI rumble, not DualSense haptics.
   - `SetLightBar` forwards through SDL on DualSense and is otherwise a no-op.
   - `SetTriggerEffect` returns OK and does nothing (post-1.0).
   - Each of these is logged once, so no call silently stubs without a trace.
8. **Guest keyboard and mouse.** `libSceMouse` and `libSceKeyboard` exports call `Unsupported()`, which logs and aborts, unless the M1 inventory shows a gate title imports them. No throw crosses the `APS5_VABI` boundary ([threading.md](threading.md)). If one does, they read the same `InputHub` events.

## Interfaces

| Peer spec | Contract |
|---|---|
| [configuration.md](configuration.md) | The `[input]` section, its typed validation, and the `APS5_NO_PAD_INPUT` replacement (`debug.ignore_host_input` during recorded-input replay). |
| [save-data.md](save-data.md) | The user IDs from `libSceUserService` that slots map to. |
| [threading.md](threading.md) | `scePadRead` takes one short host mutex, with no global guest lock. Events are processed on the window thread. |
| [gpu-driver.md](gpu-driver.md) | Presentation shares the window loop that pumps SDL events. The Presenter must not block event pumping for more than one frame. |
| [build-toolchain.md](build-toolchain.md) | SDL CMake options (`SDL_JOYSTICK` and `SDL_HIDAPI` on from M2) and the SDL pin check. |
| [verification.md](verification.md) | Recorded-input replay injects at `InputHub`, below the device layer, so replays are identical on any host controller. |

## Failure modes

| Failure | Handling |
|---|---|
| No controller and keyboard/mouse only | Slot 0 is still connected, with keyboard/mouse bindings. |
| Controller unplugged mid-game | `connected = false` for that slot, and the title shows its own reconnect prompt. Replugging restores the slot. |
| Two controllers on one slot, via keyboard merge | Buttons are OR-merged. For the sticks, the value furthest from centre wins. |
| DualSense on an SDL build without HIDAPI | Falls back to generic DirectInput mapping, with a warning logged. This is a failure in the gate matrix. |
| Window loses focus | Every key is released and mouse-look is released (current behaviour, `PadInput.cpp:24-29`). |
| An invalid binding name in TOML | Start-up error naming the key and the file (see [configuration.md](configuration.md)). |

## Tests

- **GoogleTest Unit Suites** (`ctest -L unit`, hosted `unit` job):
  - [x] Button OR-merging and stick displacement arbitration rules (`PadHapticsTests.cpp`).
  - [x] Radial and axial dead-zone mathematics and clamp boundaries (`PadHapticsTests.cpp`).
  - [x] Controller button/axis/trigger translation, slot merge with keyboard, and per-slot connect/disconnect through `scePadRead` (`tests/input/ControllerInputTests.cpp`, synthetic samples, no device).
  - [ ] Slot assignment and reassignment across plug and unplug sequences via synthetic SDL event injection.
  - [ ] TOML controller binding parsing and rejection of invalid identifiers.
  - [x] Monotonic timestamp advancement invariants on sequential `scePadRead` calls (`PadHapticsTests.cpp`).
  - [ ] Mouse open/read/close error contract + SDL routing + ring overflow (`libSceMouse/tests/Mouse.cpp`, M2-gated DISABLED GTest; builds in CI, enables with the M2 exports).
- **Ported Ecosystem Test Suites:**
  - [x] **KytyPS5 `PadHapticsTests`:** DualSense USB report parsing, radial deadzone calculation, motor vibration amplitude translation, and controller orientation telemetry (`core/libs/tests/PadHapticsTests.cpp`).
- **Replay determinism:** the same recorded input produces identical `PadData` sequences whatever controller backend is present.
- **Manual matrix per release:** a small table recorded in the release notes (the device classes only, no personal hardware):

| Device class | Buttons | Sticks/triggers | Hot-plug | Rumble |
|---|---|---|---|---|
| XInput pad | ✓ | ✓ | ✓ | ✓ |
| DualSense USB | ✓ | ✓ | ✓ | basic |
| Keyboard + mouse | ✓ | digital/mouse | n/a | n/a |

- **Full run:** each gate title is played to credits on one controller class, and the menus are checked on the other two.

## Milestones

| Milestone | Work |
|---|---|
| M1 | - [ ] Port PR #5's `scePadRead`. Import inventory for Mouse and Keyboard. Replace `APS5_NO_PAD_INPUT` with `debug.ignore_host_input`. Sync status (PR #28): the mouse backend (`libSceMouse/src/mouse_impl.cpp`, `include/MouseState.hpp`, `include/mouse_structs.h`) plus VideoOut routing (`libSceVideoOut/src/MouseInput.cpp`, `include/MouseInput.hpp`) are byte-identical to upstream `53bda68`; `libSceMouse/Export.cpp` still throws, so the Current-state row above stands. `tests/Mouse.cpp` is converted to GTest but DISABLED until the M2 exports land (builds in CI to pin the API). Debt: the process-global `std::mutex mouseMutex` (`mouse_impl.cpp:9`) violates the no-global-locks rule and must go with the M2 work. |
| M2 | - [ ] Everything in the target design. XInput, DualSense USB and keyboard/mouse pass the matrix on Dreaming Sarah and TMNT (the F5 delivery milestone). |
| M3–M5 | - [ ] Regression only. Add analog-trigger and multi-button coverage as the 3D titles demand. |
| M6 | - [ ] The release matrix is published in the release notes. |

## Open questions

- Do any gate titles need a second local player (TMNT supports co-op)? 1.0 gates single-player only. A second slot works in the design but is not gated.
- Gyro through `SDL_SENSOR`: the PRD puts only haptics and adaptive triggers out of scope. Does any gate title need motion?
- Should rumble on XInput stay on by default? It adds no DualSense-specific behaviour.

## Controller polling (implemented)

- Attach: `SDL_CONTROLLERDEVICEADDED` -> `SDL_GameControllerOpen` -> lowest free slot; `REMOVED` frees it. A fifth controller is ignored.
- Buttons: A/B/X/Y = Cross/Circle/Square/Triangle, START = Options, shoulders = L1/R1, stick clicks = L3/R3, D-pad, touchpad click = TouchPad bit.
- Sticks: -32768..32767 -> 0..255 (centre 128) with a radial dead zone from `[input] deadzone` (default 0.08), rescaled so output starts at 0 at the zone edge.
- Triggers: 0..32767 -> 0..255 in `analog_buttons_l2/r2`; the digital L2/R2 bit is raised above 30/255.
- Slot 0 merge: buttons OR, sticks furthest from centre, keyboard R2/L2 still force 255.
- `debug.ignore_host_input`: controllers are never attached while it is set. On window focus loss every attached controller publishes neutral state.
- Slots 1..3 can be opened with `scePadOpen` only while a controller occupies them.
