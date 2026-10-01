# PortPS5 — Product Requirements Document (1.0)

Status: draft v1 · 2026-09-27 (§4.5 added 2026-10-01) · Owner: PortPS5 maintainer

## 1. Summary

PortPS5 converts a user-supplied, already-decrypted PS5 game dump into a native Windows executable plus replacement system libraries. It runs the game's own x86-64 code directly on the host CPU, links it against high-level reimplementations of the PS5 system libraries, and translates the GPU workload to Vulkan. There is no emulator process and no CPU emulation.

PortPS5 is a GPL-2.0-only hard fork of [AnyPS5](https://github.com/boykopovar/AnyPS5) `main`. It selectively adopts the architecture from AnyPS5 PR #5 (the Demon's Souls boot work, merged into AnyPS5 `main` as `7656458`) and rejects that work's title-specific workarounds.

## 2. Problem

- PS5 titles cannot run on PC unless the publisher ships a port.
- Existing PS5 translation projects are research-grade. AnyPS5 reaches Demon's Souls' intro cinematic, but:
  - the path there relies on per-title kernel matching and about 300 `APS5_*` environment switches (AnyPS5 `main` after the PR #5 merge; `main@e06dbff` had none);
  - `main@e06dbff` had no CI (`main@75a8668` builds and runs `ctest` in `.github/workflows/build.yml`);
  - on `main@e06dbff`, guest locking was serialized through a process-global mutex (merged PR #5 removes that lock);
  - depth/stencil is rejected, and indirect draws did not exist on `main@e06dbff`; merged PR #5 adds them with a CPU record-reading fallback;
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

Status as of 2026-09-30: a requirement is ticked only when a gate-title run proves it, so none is ticked yet. Code that moves each one is on `main` for F1 (relinker, `--to-intel`), F2 (per-title saves, crash safety, scripted dialogs), F3 (AudioOut2, ATRAC9), F5 (SDL controllers in `scePadRead`), F6 (config schema and validation, not yet wired at startup) and F8 (offline NP, trophies, dialogs). F4 has the recompiler saveexec fix and an offline AvPlayer state machine only. F7 (disk pipeline cache) and F9 (frame-time log, watchdog, structured logs) have no runtime code yet. The per-milestone detail and open items are in [ROADMAP.md](ROADMAP.md).

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

### 4.5 Performance architecture invariants

The 1.0 bar stays at 30 fps (§4.3), and 60/120 fps stays a non-goal (§5). A design that rules out 60 fps on the reference tier is still rejected, because removing it after 1.0 would mean rewriting a subsystem. In steady state (after `warmup.end`, outside loading) the runtime holds these invariants:

| ID | Invariant | Owner spec |
|---|---|---|
| P1 | No CPU wait on GPU completion and no GPU→CPU readback inside a frame, unless the guest asked for it (a label wait, a query read, or CPU access to a GPU-written range). | [gpu-driver.md](spec/gpu-driver.md) |
| P2 | No Vulkan object creation (image, buffer, memory, descriptor pool, pipeline) per draw or per dispatch. Caches and pools reuse them. | [gpu-driver.md](spec/gpu-driver.md), [pipeline-cache.md](spec/pipeline-cache.md) |
| P3 | No shader or pipeline compilation on the submit thread after warm-up. With a warm cache this is F7. | [pipeline-cache.md](spec/pipeline-cache.md) |
| P4 | No process-global lock on a hot path. Uncontended guest synchronization stays in user mode. | [threading.md](spec/threading.md) |
| P5 | Per-draw CPU cost does not grow with guest resource size. Unchanged guest memory is not compared or copied in full when the write tracker can prove it unchanged. | [gpu-driver.md](spec/gpu-driver.md), [guest-memory.md](spec/guest-memory.md) |
| P6 | CPU recording of frame N+1 can overlap GPU execution of frame N, with an explicit, bounded number of frames in flight. | [gpu-driver.md](spec/gpu-driver.md) |

Rules:

- Telemetry counts violations of P1, P2, P3 and P5 ([spec/verification.md](spec/verification.md) §4.4). The counts appear in the results JSON but are not part of the 1.0 pass rule.
- A known violation has a bean and a line in its owner spec's Current state. The M5 performance pass removes or justifies each one.
- A speed-up never comes from skipping work ([.agents/rules/no-title-hacks.md](../.agents/rules/no-title-hacks.md)). A performance claim needs a before/after measurement with the same build flags and run protocol.

## 5. Scope

**In 1.0:** F1–F9, Windows only, the five gate titles.

**Non-goals for 1.0.** These are post-1.0 and may be picked up once 1.0 ships:

- Relink-time (ahead-of-time) shader compilation. The disk cache covers the stutter goal.
- An array-of-bytes (AOB) patch engine for fps unlocks and delta-time patches.
- Upscaling and resolution targets: FSR1/CAS, DLSS or FSR2+ via PSSR/TAA intercept, 4K, 60/120 fps. The §4.5 invariants keep these reachable without a rewrite.
- Other platforms and front ends: Linux release builds (planned for 2.0, §10), macOS, a GUI launcher. In 1.0, new code goes through the host platform layer ([spec/host-platform.md](spec/host-platform.md)) so Linux is a new backend, not a rewrite.
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

## 10. 2.0 goals (draft)

Status: draft, 2026-10-01. Nothing here changes the 1.0 scope above. 2.0 work starts after 1.0 ships (ROADMAP Part II), except the v1 seams and spikes that ROADMAP Part I lists.

**Objective.** An open-world AAA tier: GTA VI completes start-to-credits at an average of at least 30 fps and a 1% low of at least 20 fps, at 1920×1080 or higher with a warm pipeline cache, on the 2.0 reference tier, on Windows and native Linux. The definitions in §4.3 apply unchanged.

### 10.1 Gate titles (2.0)

Each title is pinned at dump time, as in §4.1. A title nobody can dump yet stays unpinned and doesn't block the milestones before its own.

| # | Title | What it proves | Title ID | Region | Patch |
|---|---|---|---|---|---|
| 6 | Horizon Forbidden West | Open-world streaming and residency without mandatory ray tracing | TBD | TBD | TBD |
| 7 | Ratchet & Clank: Rift Apart | Fast I/O and decompression, ray tracing in its RT modes | TBD | TBD | TBD |
| 8 | Marvel's Spider-Man 2 | Open world with ray tracing always on, traversal streaming | TBD | TBD | TBD |
| 9 | Grand Theft Auto VI | The 2.0 objective | TBD (not released as of 2026-10-01) | TBD | TBD |

**GTA VI pin risk.** New titles often require console firmware newer than any that users can currently dump from. GTA VI may stay undumpable long after release. Gates 6–8 carry the 2.0 mechanisms meanwhile, and gate 9 is pinned when a user-owned dump exists. No 2.0 code may depend on GTA VI-specific knowledge before then, or after ([spec/README.md](spec/README.md) global policy and [.agents/rules/no-title-hacks.md](../.agents/rules/no-title-hacks.md)).

### 10.2 Functional requirements (2.0)

| ID | Requirement |
|---|---|
| V1 | - [ ] Native Linux: the CLI converts on Linux, and the runtime runs the 1.0 gate titles and the 2.0 gate titles on Linux, with the same pass rules. |
| V2 | - [ ] Streaming and residency: guest memory beyond host VRAM is handled by a budgeted residency manager, with no stall over 1 s (§4.3) caused by eviction. |
| V3 | - [ ] Decompression: the titles' hardware-decompression requests are served off the guest thread. |
| V4 | - [ ] Ray tracing: guest acceleration structures and ray queries run on Vulkan ray tracing. Skipping RT work is not a pass. |
| V5 | - [ ] Modern geometry: primitive (NGG) and mesh shader stages translate. |
| V6 | - [ ] Async compute on a separate host queue, and bounded CPU/GPU frame overlap (invariant P6). |

### 10.3 2.0 reference tier

The 1.0 tier (§4.4) is not expected to carry an open-world AAA title at 30 fps, because translation adds overhead the console doesn't have. The 2.0 tier is anchored by benchmark floors the same way as §4.4, with ray-tracing support required. The floor values are set from the 1.0 M5 performance-pass data, not guessed (ROADMAP M7). No specific personal machine is part of the specification.

### 10.4 2.0 non-goals

60/120 fps targets, upscalers and frame generation, and the AI-driven optimization ideas stay out of 2.0. A future profile-guided optimization would be a locally derived cache like the pipeline cache, never per-title code.

### 10.5 2.0 risks

| ID | Risk | Mitigation |
|---|---|---|
| V-R1 | GTA VI can't be dumped for a long time. | Gates 6–8 carry the mechanisms; gate 9 waits for its pin. |
| V-R2 | Guest `fs`-segment TLS conflicts with glibc on Linux. | Spike before M7 (bean `portps5-u16x`, [spec/host-platform.md](spec/host-platform.md) open question 1). |
| V-R3 | The guest BVH layout can't be translated to Vulkan acceleration structures efficiently. | Spike against the public RDNA2 BVH layout in Mesa RADV (MIT) before M9 (bean `portps5-mdu8`). |
| V-R4 | Decompression formats are proprietary. | Use only implementations with a GPL-2.0-compatible licence, or the title's own software path; record the licence before adopting (§6, R1). |
