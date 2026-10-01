# PortPS5 — Roadmap to 1.0

Status: draft v1 · 2026-09-27 · checkboxes synced with `main` on 2026-09-30

Status as of 2026-09-30: M0 is done except the upstream-baseline check, a local-only run (bean `portps5-sigm`). M1 work proceeded ahead of that check; the rule that a milestone starts after the previous exit criteria pass is not waived, so the baseline must be recorded before M1 can exit. M1 has landed most ports (recompiler, relinker, threading, libc, image codecs, offline services, golden corpus, Recorder and HostImport building blocks, write tracker building blocks) but not the runtime wiring (Recorder in the submit path, arena and heap on the extent allocator, config at startup, telemetry). M2 has landed save data fidelity, SDL controllers in `scePadRead` and depth/stencil state decode. Landed late on 2026-09-30: kernel sync ports (PR #61), the ElfReader wrap-free fix (PR #64), the single audio mixer (PR #45) the official Doxygen install (PR #69) and the host depth/stencil surface (PR #55). In flight and **not** landed: `/savedata0` mount (PR #53) and Vulkan 1.3 floor (PR #62), plus unrelated CI and libc fixes (PRs #66, #67, #68). Every unticked item below names a bean (`.beans/`) or an open PR where one exists; M3 and later items get beans when their milestone seed is written.

This roadmap is phased, with no calendar dates. Capacity is a solo maintainer plus AI agents, part-time. Each milestone has measurable exit criteria and unlocks gate titles in order of risk. A milestone starts only when the previous one's exit criteria pass. Each milestone becomes its own `ooo seed`.

## Milestone 0: Fork foundation

**Scope**
- [x] Rebase `yuriolive/PortPS5` onto AnyPS5 `main` history (`e06dbff`), and credit AnyPS5 in the README.
- [x] Keep the GPL-2.0-only licence.
- [x] Change the build: bump to C++23, add CMakePresets, pin the MinGW GCC 15.2 toolchain, copy the MinGW runtime DLLs next to the patched prx, and wire every existing test into `ctest`.
- [x] Standardize test framework on GoogleTest (GTest + GMock) via FetchContent with `gtest_discover_tests`; establish `tests/common/TestHarness.hpp` (SCE matchers, error code formatters, guest page fixtures); modernise legacy standalone `abort()` test binaries into structured GTest suites; update CTest expected counts ([TESTING.md](TESTING.md), [spec/verification.md](spec/verification.md) §5).
- [x] Make glslang test-only by moving `tests/DummyShaders.cpp` out of the shipped recompiler library ([spec/build-toolchain.md](spec/build-toolchain.md), PRD R1).
- [x] Replace the no-comments rule in CONVENTIONS with "comment why, not what".
- [x] Hosted CI jobs `build`, `unit`, `policy`, and `python-quality` ([spec/verification.md](spec/verification.md) §1); Python tooling bar (`uv`, `ruff`, `pytest` coverage) and RDNA ISA progress telemetry (`tools/progress.py`, `rdna_isa.txt`, workflows and badges).
- [x] Dump the five gate titles and record their pins in the PRD §4.1 table. Swap any PS4-only build within the same tier (PRD R4).

**Exit criteria**
- [x] `main` builds from a clean clone in CI, and `ctest` is green with GoogleTest runner reporting individual test cases.
- [x] The PRD pin table has no TBD cells.
- [ ] Baseline preserved. At the start of M0, run Demon's Souls on upstream `main` (`e06dbff`) and record the furthest observable stage (for example "SIE logo presented"). After the rebase, the fork reaches that same stage. No baseline stage is recorded in the repo (bean `portps5-sigm`).

## Milestone 1: Selective port of merged AnyPS5 PR #5 and runtime core

**Scope**
- [ ] Port from AnyPS5 `main` (merged PR #5) onto the fork, without its title-specific code:
  - [x] the Recorder (adapted `Recorder` landed in `Graphics/`, PR #49). The building block only: `sceAgcDriverSubmitDcb` does not record through it yet (bean `portps5-tiod`);
  - [x] host import with a staging fallback (`HostImport`, PR #49; unwired, same bean);
  - [x] GPU detile (`Graphics/src/TextureDetiler.cpp`);
  - [ ] GuestArena/GuestHeap behind `IWriteTracker`. Building blocks landed (extent allocator PR #32, `WriteWatchTracker` with page-state table and pins PR #40) but the runtime instantiates none of them yet (bean `portps5-421p`);
  - [x] `--to-intel` SSE4a lowering;
  - [x] AudioOut2 and ATRAC9;
  - [x] the recompiler fixes (saveexec order, atomic-zero, LDS barriers);
  - [x] the `agc_shader_replay` tool (adapted, PR #31) and request serialisation (`RequestSerializer`, in the tree since the fork).
- [ ] Establish foundational subsystem GoogleTest suites ported and adapted from open-source ecosystem references:
  - [x] Kernel synchronization & threading: port futex/umtx, pthread mutex/condvar/rwlock priority, and `WaitOnAddress` race perturbation tests from FreeBSD 12, Wine, and KytyPS5 (`SyncOnAddressTests`) ([spec/threading.md](spec/threading.md));
  - [x] Event queues: port kqueue/kevent edge/level triggers, user events, and timeout cancellation tests from FreeBSD 12 and KytyPS5 (`EventQueueLifetimeTests`);
  - [ ] Virtual memory ([spec/guest-memory.md](spec/guest-memory.md)):
    - [ ] port 16 KB page rounding, direct memory mapping and protect state transitions from FreeBSD 12 and KytyPS5 (`VirtualMemoryAllocationTests`: 241 of Kyty's 3,173 lines ported so far; bean `portps5-vzmm`);
    - [x] port the memory-tracking behaviour from KytyPS5 `MemoryTrackerTests` onto `GuestMemoryTracking::Watch` (`tests/memory/MemoryTrackerTests.cpp`: range validation, page rounding, protection/fault resolution, invalidate, resolver contract, concurrency). Kyty's CPU/GPU dirty-ownership, upload/download ranges, `RangeSet` and region-mask batching have no counterpart until the `IWriteTracker` implementation lands, so they are not ported;
    - [x] `IWriteTracker` write-watch tests (`Collect`, `MarkWritten`, generations, pins, flush hook, page-state table): `core/libs/tests/WriteTracker.cpp`, against the real `WriteWatchTracker` (PR #40). Aliased-view tests stay open until alias support exists (M5, [spec/guest-memory.md](spec/guest-memory.md); bean `portps5-421p`).
  - [x] Kernel error contract, timedwait, thread-self, and host TLS: verify POSIX/SCE errno families, `ETIMEDOUT` / `SCE_KERNEL_ERROR_ETIMEDOUT` conventions, and Win32 TLS isolation (`GuestKernelErrors`, `GuestCondTimedwait`, `GuestThreadSelf`, `HostThreadLocal`) ([spec/threading.md](spec/threading.md));
  - [ ] Shader recompiler: port instruction decoding bitfield validation, DPP swizzles, SDWA packing, 64-bit LDS, and divergent control-flow tests from Mesa ACO and KytyPS5 (`ShaderRecompilerComputeTests`, `shaderCfgTests`) ([spec/shader-recompiler.md](spec/shader-recompiler.md)). Only partly landed, so this was unticked on 2026-09-30: `RecompilerFixesTests` (saveexec, atomic-zero, LDS barriers, divergent regions), `RdnaPortedInstructionTests` (PR #60) and the synthetic golden corpus exist; no `ShaderRecompilerComputeTests`, `shaderCfgTests`, Mesa ACO or decoder `TEST_P` suites exist in the tree (bean `portps5-3asd` covers what landed and is completed; the remaining ports are bean `portps5-qxip`).
- [ ] Interim FMV correctness: AnyPS5 `main`'s Bink-plane write-back (merged PR #5) is ported as a general mechanism, *adjacent block-generation advance*, with no switch and no title reference. It serves M1–M2 and is replaced in M3 ([spec/video-fmv.md](spec/video-fmv.md)). Not implemented: there is no adjacent-generation code under `libSceAgcDriver/Graphics` on main (bean `portps5-ux18`).
- [ ] Recompiler: bindless tables with bounds taken from device limits ([spec/shader-recompiler.md](spec/shader-recompiler.md); bean `portps5-7li6`).
- [x] Relinker: make the existing `.eh_frame`-seeded CFG (`CodeInstructionCollector`) the only instruction-discovery engine, as one shared `CodeMap` replacing the linear sweep ([spec/relinker.md](spec/relinker.md)); support Linux load-alignment and `--windows-gui` PE subsystem switch.
- [x] Export ABI: the `APS5_EXPORT_FN` export macro, which declares every export `APS5_VABI` and `noexcept` ([spec/build-toolchain.md](spec/build-toolchain.md)).
- [ ] Guest memory: replace the arena's O(n) first-fit scan with a free-list allocator; add explicit pins, the registry-owned page-state table, and return codes in place of throws ([spec/guest-memory.md](spec/guest-memory.md)). Parts landed as unwired building blocks (bean `portps5-421p` tracks the wiring):
  - [x] extent-tree allocator core with a 10^6-operation differential test (PR #32, `GuestArenaExtent`);
  - [x] explicit pin tokens and the page-state table in `WriteWatchTracker` (PR #40);
  - [x] return codes instead of throws in direct memory and memory pools (PR #39);
  - [ ] the arena, heap spans and registry use them, and the runtime instantiates the tracker.
- [x] Offline behaviour for network stack (`libSceNet`), AvPlayer playback state machine (`libSceAvPlayer`), NP/PSN, trophies, store, and user-service dialogs, so no gate title blocks on them at boot.
- [x] Rewrite pthread/umtx/cond on futex words (`WaitOnAddress`), with no global mutex and compact guest tids, and make errno returns correct. Unimplemented exports call `Unsupported()`, which logs and aborts; no throw crosses the `APS5_VABI` boundary ([spec/threading.md](spec/threading.md)).
- [x] Image codecs: shared stb-backed JPEG/PNG layer (`core/Decoder`), `libSceJpegEnc` and `libScePngDec` ([spec/image-codecs.md](spec/image-codecs.md)); `libScePngEnc` remains open.
- [ ] Per-game TOML config, with `display.present_mode` and `display.resolution_scale` wired. Remove the `APS5_*` behaviour switches; keep a typed `[debug]` section.
  - [x] `Config` schema, validation and the typed `[debug]` section (`libc/src/Config.cpp`, `core/libs/tests/Config.cpp`); no `APS5_` string literals remain in `core/` and the `policy` job enforces it;
  - [x] startup loads config for the `param.json` title ID before guest initializers (startup portion of bean `portps5-c06p`);
  - [ ] `display.present_mode` and `display.resolution_scale` reach the driver: the swapchain is hard-coded to FIFO (bean `portps5-dtwf`).
- [ ] Runtime telemetry: frame-time log, watchdog, structured logs, audio underrun and latency counters, and the A/V offset skeleton (`video_latency_ms`). Only per-context audio underrun and overrun counters exist (bean `portps5-f9a3`).
- [x] Hosted CI job `recompiler-golden` (synthetic corpus green in CI; merged into the `build_and_test` job by PR #38).
- [ ] Hosted CI job `driver-lavapipe`: driver suites carry the `lavapipe` label but `ci.yml` has no such job (bean `portps5-ekx3`).
- [ ] Inventory each gate title's imports (NIDs, audio and video codecs, dialogs). One title of five is recorded; a repeatable tool (bean `portps5-zadg`) and the other four (bean `portps5-3eh1`) are open.
  - Dreaming Sarah (recorded 2026-09-29): 815 relocation refs / 484 unique NIDs, all 484/484 resolve at link time to built prx exports (6 libc locale/iostream data symbols resolve to stubs/host-backed placeholders); `libSceVideoOut` `Config::Loader` verbatim exports resolved. Detail in [spec/relinker.md](spec/relinker.md) Open questions.

**Exit criteria**
- [ ] (bean `portps5-0mv7`) With title-specific code removed, Demon's Souls reaches the in-engine intro cinematic, the stage AnyPS5 PR #5 reached. Its fill and copy kernels run as the title's own shaders, without replacement. The `policy` CI job is green.
- [x] Sync microbenchmark: uncontended lock/unlock at least 10× faster than the old implementation, and the pthread and `SyncOnAddress` GoogleTest suites pass.
- [ ] The hosted golden corpus contains synthetic shaders that cover every decoded instruction class, and it is green in CI. The local-only game-derived corpus replays with 0 validation failures.

## Milestone 2: 2D gate (titles 1–2)

**Scope**
- [ ] Input: XInput, DualSense over USB, and keyboard/mouse mapping in TOML ([spec/input.md](spec/input.md); bean `portps5-de24`).
  - [x] SDL joystick/HIDAPI are enabled (`CMakeLists.txt:46-48`), and SDL game controllers feed `scePadRead` with hot-plug, slot assignment, a radial dead zone and keyboard merge (PRs #54 and #57, tests in `tests/input/ControllerInputTests.cpp`). Not verified with a physical controller;
  - [x] mouse backend (`libSceMouse/src/mouse_impl.cpp`) and VideoOut routing exist, but the `sceMouse*` exports are still `Unsupported()` stubs and `mouse_tests` is `DISABLED` (bean `portps5-afme`, low priority);
  - [ ] TOML keyboard/mouse bindings (only `[input] deadzone` is consumed), XInput and DualSense USB on the manual matrix, slot reassignment tests with injected SDL events.
- [ ] Establish filesystem sandbox, input, and audio GoogleTest suites ported from ecosystem references:
  - [x] Filesystem sandbox: port path-traversal containment (`../`), mount sandbox isolation, and default-deny permission tests from SharpEMU (`KernelSandboxEscapeTests`) ([spec/save-data.md](spec/save-data.md));
  - [x] Save data: port directory layout, quota enforcement, atomic commit, and crash-safe snapshot restore tests;
  - [x] Input: port DualSense USB report parsing, radial deadzone calculation, rumble motor translation, and hotplug slot assignment tests from KytyPS5 (`PadHapticsTests`) ([spec/input.md](spec/input.md));
  - [x] Audio: port AudioOut2 port lifecycle, ATRAC9 header decoding, and mixer resampling tests from KytyPS5 (`AudioOut2PortTests`) ([spec/audio.md](spec/audio.md)).
- [x] Save data: dialogs return scripted and logged results instead of silent stubs; saves are stored per title, with crash-safe snapshots and a one-time copy of the old `_sd` layout ([spec/save-data.md](spec/save-data.md)).
- [x] Audio: a single host mixer with a resampler and a soft limiter, on one device clock ([spec/audio.md](spec/audio.md)).
- [ ] The disk pipeline cache (bean `portps5-8gdr`).
- [x] Driver: depth/stencil and conditional colour-write state decode, `VkPipelineDepthStencilStateCreateInfo` emission and pipeline-cache keying (`Graphics/src/State.cpp`).
- [x] Driver: host depth/stencil surface (`DB_Z_INFO` decode, host `VkImage`, render-pass attachment, `DB_RENDER_CONTROL` clears, depth bounds, `DB_DEPTH_CONTROL` bit 31 colour suppression) (`Graphics/src/DepthSurface.cpp`, [spec/gpu-driver.md](spec/gpu-driver.md); PR #55, bean `portps5-mij8` completed).
- [ ] Per-title `/savedata0` mount for guest file I/O: no mount exists on main. In flight as PR #53, not landed (bean `portps5-10fr`).
- [ ] Driver: retile depth to guest memory (CPU or shader reads of a depth buffer) and guest-memory upload of never-cleared depth surfaces. A draw that depth-tests a surface that was never cleared is rejected until then (bean `portps5-9s7e`).
- [ ] `tools/regress` local regression plus results JSON upload, with the config hash and the "FMV played" rule (bean `portps5-3m3u`).

**Exit criteria**
- [ ] Dreaming Sarah and TMNT: Shredder's Revenge pass the full-run protocol (average ≥30 fps, 1% low ≥20 fps, 1080p, 0 crashes and 0 softlocks, save round-trip). Dreaming Sarah: bean `portps5-kmb6`.
- [ ] Their results JSON is published, and the compatibility list is generated.

## Milestone 3: 3D core (title 3)

**Scope**
- [ ] Driver:
  - split into CommandProcessor, Recorder, Buffer/Texture/Pipeline caches, Rasterizer and Presenter;
  - a GPU-side path for the `DRAW_INDIRECT` family (AnyPS5 `main@75a8668` takes `vkCmdDraw[Indexed]Indirect[Count]` only when the record fold, shader path, draw index and memory state allow it, and otherwise reads records on the CPU: `Draw.hpp:43`, `Draw.cpp:823-830`);
  - general block-generation write tracking for GPU-written surfaces, replacing the interim adjacent block-generation advance from M1–M2. Tomb Raider and Bugsnax FMV depend on it;
  - redesign capture ordering: resolve buffers on the GPU, resolve images at submit time, and never satisfy a wait from an unexecuted label while a capture depends on it.
- [ ] Establish Vulkan driver cache and recompiler structurizer GoogleTest suites:
  - Driver resource caches: port buffer/texture cache overlap, staging ring-buffer exhaustion, and descriptor set lifecycle tests adapted from DXVK and RPCS3 patterns ([spec/gpu-driver.md](spec/gpu-driver.md));
  - Structurizer: recompiler fuzz corpus and synthetic unstructured control-flow graphs covering irreducible loops and goto-elimination fallbacks ([spec/shader-recompiler.md](spec/shader-recompiler.md)).
- [ ] Recompiler: goto-elimination structurizer fallback, and bounded hash-indexed variants.
- [ ] Save data: the multi-slot list dialog, for Tomb Raider save/load ([spec/save-data.md](spec/save-data.md)).

**Exit criteria**
- [ ] Tomb Raider I-III Remastered passes the full-run protocol.
- [ ] A Demon's Souls stress run of 3 × 150 s of the intro cinematic has 0 wedges, with 0 skipped dispatches from the "guest memory is not readable" class.
- [ ] Recompiler fuzz corpus: 0 structurizer throws.

## Milestone 4: Middleware (title 4)

**Scope**
- [ ] UE4 job-system coverage (event flags, semaphores, fibers).
- [ ] Establish fiber and job-system GoogleTest suites:
  - Fibers and event flags: port fiber stack-switching, fiber-local storage (FLS), and event flag race perturbation tests from SharpEMU (`Fiber*Tests`) ([spec/threading.md](spec/threading.md));
  - Wave64 subgroup operations: synthetic compute dispatch validation for 64-wide lanes ([spec/shader-recompiler.md](spec/shader-recompiler.md)).
- [ ] Wave64 through `VK_EXT_subgroup_size_control` where supported.
- [ ] A GPU-side descriptor heap for bindless (`VK_EXT_descriptor_indexing` or `VK_EXT_descriptor_buffer`).
- [ ] Fill and copy kernels recognised by general IR patterns.

**Exit criteria**
- [ ] Bugsnax passes the full-run protocol.
- [ ] Every `[workarounds]` key used by a gate title has an entry in `docs/workarounds.md`.

## Milestone 5: AAA (title 5)

**Scope**
- [ ] Demon's Souls from the first level to credits:
  - streaming and resource aliasing at full size;
  - host-import budget sized automatically (the AnyPS5 PR #5 description on GitHub reports a manual 16 GiB `APS5_HOST_IMPORT_MIB` override for later stages);
  - direct-memory aliasing (the same physical range mapped at several guest addresses) with write tracking, which `GetWriteWatch` may not cover ([spec/guest-memory.md](spec/guest-memory.md));
  - audio object-port panning ([spec/audio.md](spec/audio.md)).
- [ ] Direct-memory multi-mapping and host-import stress suites: perturbation tests covering concurrent aliased writes and memory tracker cache coherency under 16 GiB budget pressure.
- [ ] Performance pass against the bar.
- [ ] Spike: llvm-mingw clang with PDBs, adopted only if the DWARF unwinder validates.

**Exit criteria**
- [ ] Demon's Souls passes the full-run protocol, with title-specific behaviour only in its documented TOML.

## Milestone 6: 1.0 release

**Scope**
- [ ] A release full run of all five titles on the release commit.
- [ ] Release docs: user guide (CLI usage, the legal boundary, the ban on sharing converted executables), `docs/workarounds.md`, and the compatibility list.
- [ ] Record the PRD R1 licence risk status in the release notes.

**Exit criteria**
- [ ] 5/5 gate titles pass the full-run protocol on the release commit.
- [ ] Hosted CI and local regression are green.
- [ ] The release notes are published.

## Traceability

| 1.0 goal (PRD) | Delivered in | Verified by |
|---|---|---|
| F1 local CLI conversion | M0 (existing), M1 (`--to-intel`) | CI build, M2 full runs |
| F2 save/load | M2 (per-title storage, scripted dialogs), M3 (multi-slot list dialog) | Save round-trip in regression and full run |
| F3 audio | M1 (AudioOut2, ATRAC9, codec inventory), M2 (single mixer, resampler, limiter), M5 (object-port panning) | Underrun telemetry, full runs |
| F4 FMV | M1 (saveexec fix, interim adjacent block-generation advance), M3 (general block tracking) | FMV frame checks, A/V offset telemetry, full runs |
| F5 input (XInput, DualSense, keyboard) | M2 | Full runs |
| F6 per-game TOML | M1 | `policy` CI job, `docs/workarounds.md` |
| F7 disk pipeline cache | M2 | Warm-cache full runs |
| F8 offline PSN/trophies | M1 | Full runs never blocked |
| F9 telemetry | M1 | Results JSON present for every run |
| Performance bar (§4.3) | M1 (sync), M2 (depth/stencil), M3 (driver), M4 (subgroups), M5 (perf pass) | Results JSON fps stats |
| Ecosystem test suites (GTest, Kyty, SharpEMU, FreeBSD, Mesa, Wine) | M0 (framework), M1 (core runtime), M2 (sandbox/input/audio), M3 (caches/structurizer), M4 (fibers/jobs) | CI `unit`, `recompiler-golden`, `driver-lavapipe` |
| Gate 1: Dreaming Sarah | M2 | Full run |
| Gate 2: TMNT: Shredder's Revenge | M2 | Full run |
| Gate 3: Tomb Raider I-III Remastered | M3 | Full run |
| Gate 4: Bugsnax | M4 | Full run |
| Gate 5: Demon's Souls | M5 | Full run |
| No title-specific code | M1 (policy), all milestones | `policy` CI job |
| Hosted CI without GPU; local results | M0, M1, M2 | Verification spec |
| R1 licence risk documented | PRD §7, M6 release notes | Doc review |
