# PortPS5 — Product Requirements Document (1.0)

Status: draft v1 · 2026-09-27 · Owner: PortPS5 maintainer

## 1. Summary

PortPS5 converts a user-supplied, already-decrypted PS5 game dump into a native Windows executable plus replacement system libraries. It runs the game's own x86-64 code directly on the host CPU, links it against high-level reimplementations of the PS5 system libraries, and translates the GPU workload to Vulkan. There is no emulator process and no CPU emulation.

PortPS5 is a GPL-2.0-only hard fork of [AnyPS5](https://github.com/boykopovar/AnyPS5) `main`. It selectively adopts the architecture from AnyPS5 PR #5 (the Demon's Souls boot work) and rejects that PR's title-specific workarounds.

## 2. Problem

- PS5 titles cannot run on PC unless the publisher ships a port.
- Existing PS5 translation projects are research-grade. AnyPS5 reaches Demon's Souls' intro cinematic, but:
  - the path there relies on per-title kernel matching and about 300 `APS5_*` environment switches (PR #5; `main` has none);
  - there is no CI;
  - on `main`, guest locking is serialized through a process-global mutex (PR #5 removes that lock);
  - depth/stencil is rejected, and indirect draws exist only in PR #5, as a CPU record-reading fallback;
  - there is no gamepad input (XInput/DualSense), and all AvPlayer exports throw.
- Nobody has shown a PS5 title completed start-to-credits.

## 3. Users

| User | Needs |
|---|---|
| Player who owns a PS5 and its games | Convert their own dumps locally with a CLI, then play with a controller at stable frame rates. |
| Contributor | A buildable, tested codebase with clear subsystem ownership and a public compatibility list. |
| Maintainer (solo, plus AI agents, part-time) | A roadmap that front-loads risk, with automated checks that do not need a GPU in CI. |

## 4. Goals (1.0)

1.0 ships when all five **gate titles** below are completable start-to-credits and meet the performance bar on the reference PC tier.

### 4.1 Gate titles

Each title is pinned at dump time. Its title ID, region and patch version are recorded here, and later results are only valid against that pin.

| # | Title | Tier / engine | What it proves | Title ID | Region | Patch |
|---|---|---|---|---|---|---|
| 1 | Dreaming Sarah | 2D, custom | PM4 basics, blits, presentation, file streaming | PPSA02929 | US | 01.000.000 |
| 2 | TMNT: Shredder's Revenge | 2D action | Sprite throughput, pad input, audio mixing, 60 Hz pacing | PPSA06731 | US | 01.000.000 |
| 3 | Tomb Raider I-III Remastered | Simple 3D | Depth buffer, 3D transforms, texture sampling, save/load | PPSA16902 | US | 01.000.000 |
| 4 | Bugsnax | Unreal Engine 4 | UE4 job system, dynamic buffers, shadow passes | PPSA01502 | US | 01.000.000 |
| 5 | Demon's Souls | AAA custom engine | Async compute, resource aliasing, streaming, Bink FMV, indirect draws | PPSA01342 | US | 01.000.000 |

A title is replaced only by one of the same tier. For example, if a title's dump turns out to be a PS4 (CUSA) build rather than a native PS5 (PPSA) build, it is swapped for a PS5-native title of the same tier. Any swap is recorded in this table with a reason.

### 4.2 Functional requirements

| ID | Requirement |
|---|---|
| F1 | - [ ] A CLI converts a decrypted dump directory into a runnable Windows executable plus runtime libraries, locally, with clear errors for unsupported inputs. |
| F2 | - [ ] Save/load works: the player saves in game, quits, relaunches and continues from the save. |
| F3 | - [ ] Audio plays correctly, including ATRAC9 and the title's mixing path. During FMV the audio/video offset stays within ±80 ms, and there is at most 1 audio underrun per 10 minutes (both logged by telemetry). |
| F4 | - [ ] FMV plays: in-game video (e.g. Bink) decodes and plays. Skipping a video is not a pass. |
| F5 | - [ ] Input: XInput controllers, DualSense over USB, and keyboard/mouse mapping. |
| F6 | - [ ] A per-game TOML config keyed by title ID holds resolution scale, present mode, and any documented per-title workaround. |
| F7 | - [ ] A disk pipeline cache keeps compiled shaders and pipelines across runs. With a warm cache, telemetry logs 0 shader compilations and 0 pipeline creations during a regression pass. |
| F8 | - [ ] Online, PSN and trophy calls return offline behaviour and never block progression. |
| F9 | - [ ] Runtime telemetry covers frame-time log, crash and softlock watchdog, and structured logs. It feeds the verification protocol. |

### 4.3 Performance bar

Measured on a full run with a warm pipeline cache, at 1920×1080 or higher output resolution:

- average frame rate of at least **30 fps**;
- **1% low** of at least **20 fps**;
- **0 crashes and 0 softlocks** from boot to credits.

Definitions (mechanical; no title knowledge needed):

- **1% low** = 1000 / (mean of the slowest 1% of frame times in ms), computed over the whole run. A run must contain at least 30 minutes of presented frames.
- **Stall:** any gap of more than 1 s between presents. Stalls are excluded from the fps statistics and reported separately as a stall count.
- **Softlock:** no present, or no guest thread progress, for more than 30 s. A long load counts as a softlock, and there is no loading-screen exemption.

### 4.4 Reference PC tier

The bar is measured on a generic **upper mid-tier** desktop:

- 12-core desktop CPU;
- mid-to-high-end GPU with 12 GB VRAM and Vulkan 1.3;
- 32 GB RAM;
- Windows 11.

To keep the tier fixed over time, it is anchored by benchmark floors: CPU Cinebench R23 multi-core ≥ 18,000, and GPU 3DMark Time Spy graphics score ≥ 18,000. A machine that meets both floors qualifies. The local test script records both scores and derives `host_tier` from them instead of trusting a self-declared value. No specific personal machine is part of the specification.

## 5. Scope

**In 1.0:** F1–F9, Windows only, the five gate titles.

**Non-goals for 1.0.** These are post-1.0 and may be picked up once 1.0 ships:

- Relink-time (ahead-of-time) shader compilation. The disk cache covers the stutter goal.
- An array-of-bytes (AOB) patch engine for fps unlocks and delta-time patches.
- Upscaling and resolution targets: FSR1/CAS, DLSS or FSR2+ via PSSR/TAA intercept, 4K, 60/120 fps.
- Other platforms and front ends: Linux and macOS release builds, a GUI launcher.
- DualSense haptics and adaptive triggers.
- Titles beyond the gate set. The compatibility list will track them, but they do not gate 1.0.
- Tracking upstream AnyPS5. This is revisited after Milestone 3.

## 6. Legal boundary

- PortPS5 accepts only already-decrypted dumps that users produce from consoles and games they own.
- The project ships no keys, firmware, Sony libraries, Sony SDK headers or decryption code. It is developed only from public hardware documentation (AMD RDNA2 ISA, addrlib) and open-source projects.
- Users convert locally. The documentation and release notes state that sharing converted executables is prohibited, because they contain the publisher's code.
- CI and issue reports never receive game data. Compatibility results carry only hashes and metrics (see [spec/verification.md](spec/verification.md)).

## 7. Risks

| ID | Risk | Impact | Mitigation |
|---|---|---|---|
| R1 | **License compatibility.** The project is GPL-2.0-only. SPIRV-Tools (Apache-2.0) is statically linked into the recompiler when `ANYPS5_ENABLE_SPIRV_TOOLS` is on. That option defaults to off, but the planned validation path relies on it. Linking it may be incompatible with GPLv2-only distribution. glslang (mixed BSD/Apache/MIT) is linked into the recompiler static library through `tests/DummyShaders.cpp`, which nothing calls. It probably doesn't reach shipped binaries, but that is unverified; Milestone 0 moves it to a test target and adds a CI symbol check. Vulkan-Headers is headers only. | Blocks public binary releases that include SPIRV-Tools if unresolved. | Documented only, with no action for now. Options on record: ship release builds without SPIRV-Tools (validate only in CI and dev builds), run it out of process, or ask the AnyPS5 authors for a GPL-2.0-or-later grant. |
| R2 | Demon's Souls may not be reachable without title-specific code. | Gate title 5 slips. | The general mechanisms are scheduled early: GPU-side resource resolution, depth/stencil, indirect draws. Workarounds go only in per-game TOML. |
| R3 | CPU-side capture of guest memory at record time races with guest CPU writes and with other queues. | Intermittent hangs and corruption. | Redesign in Milestone 3, before the first 3D gate title (spec §GPU driver). |
| R4 | A gate title's dump is a PS4 build or otherwise unsupported. | Gate title must be swapped. | Verify the title ID at dump time, before any work on that title, and swap within the same tier. |
| R5 | Solo part-time capacity against a codebase heading for 180–280k lines. | Slow progress, burnout. | Milestones ordered by risk. Agent-friendly tests (golden SPIR-V, unit tests), strict scope, and no dates. |
| R6 | MinGW GCC 15.2 is the only supported toolchain, so there are no PDBs for native debuggers. | Slower debugging. | Optional llvm-mingw clang investigation (spec §Build). |
| R7 | Full playthroughs can't be automated. | Release verification takes many hours. | One manual full run per title per release, plus automated save-checkpoint regression per build. |

## 8. Success metrics

- 5/5 gate titles pass the full-run protocol against their pins.
- Hosted CI is green on `main`, and the local regression suite passes on the release commit.
- The public compatibility list is generated from uploaded results JSON.

## 9. Related documents

- [spec/README.md](spec/README.md): subsystem index and decisions, with one spec file per subsystem.
- [spec/verification.md](spec/verification.md): CI, local regression, full-run protocol and results schema.
- [ROADMAP.md](ROADMAP.md): milestones, exit criteria and traceability.
