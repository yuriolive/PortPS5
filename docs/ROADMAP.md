# PortPS5 — Roadmap (Part I: 1.0, Part II: 2.0)

Status: draft v1 · 2026-09-27 · checkboxes synced with `main` on 2026-09-30

Status as of 2026-10-01: M0 is done except the upstream-baseline check, a local-only run (bean `portps5-sigm`). M1 work started before that check passed, as a recorded exception to the start rule below; the exception covers starting only, so M1 cannot exit until the M0 baseline is recorded. M1 has landed most ports (recompiler, relinker, threading, libc, image codecs, offline services, golden corpus, Recorder and HostImport building blocks, write tracker building blocks, the telemetry core and watchdog, the `tools/regress.py` runner) but not all the runtime wiring: config loads at startup since PR #71, while the Recorder in the submit path, the arena and heap on the extent allocator, and the telemetry call sites are still open. M2 has landed save data fidelity, SDL controllers in `scePadRead`, depth/stencil state decode, the host depth/stencil surface and the single audio mixer. Landed 2026-09-30 to 2026-10-01: Vulkan 1.3 driver floor (PR #62), `driver-lavapipe` CI job (PR #82), telemetry core (PR #78), regress runner (PR #76), resident render-target image pool (PR #74), SSE4a lowering of register forms (PR #73), guest-pointer validation (PR #83), direct-memory exports (PR #84), MsgDialog, PlayGo and AudioOut2 ports (PRs #85, #86, #87), per-title `/savedata0` mount (PR #53), verbatim libc `Unsupported` export with the cross-prx import check (PR #68), AVC decode through a pinned LGPL FFmpeg (PR #88). Also landed: config load at startup and the gated frame-timing report (PR #71), the cross-prx mount-table export fix (PR #94) and DualSense output, motion and the hot-plug slot table (PR #81). In flight and **not** landed: per-draw texture cost (PRs #75, #77). Every unticked item below names a bean (`.beans/`) or an open PR where one exists; epics `portps5-4ut1` (GPU driver), `portps5-w3s8` (guest memory) and `portps5-r8mh` (runtime wiring and telemetry) group the M1 beans. Every v1 item now has a bean, grouped under milestone beans M0 `portps5-s1kj`, M1 `portps5-7dk3`, M2 `portps5-dbpx`, M3 `portps5-oo21`, M4 `portps5-rd6g`, M5 `portps5-bxvu` and M6 `portps5-w6uc`. Each bean records its blockers, so `beans list --ready` shows what can run in parallel. Part II (2.0) items get beans when their milestone seed is written.

This roadmap is phased, with no calendar dates. Capacity is a solo maintainer plus AI agents, part-time. Each milestone has measurable exit criteria and unlocks gate titles in order of risk. A milestone starts only when the previous one's exit criteria pass; any exception is recorded in the status line above and never relaxes the exit criteria. Each milestone becomes its own `ooo seed`.

# Part I: 1.0

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
    - [x] Guest-pointer range validation for PRX libraries (`GuestMemoryValidation`, first consumers `libSceJpegEnc` and `libScePngDec`; [spec/guest-memory.md](spec/guest-memory.md) "Validation API"; bean `portps5-8l0d`).
    - [x] `IWriteTracker` write-watch tests (`Collect`, `MarkWritten`, generations, pins, flush hook, page-state table): `core/libs/tests/WriteTracker.cpp`, against the real `WriteWatchTracker` (PR #40). Aliased-view tests stay open until alias support exists (M5, [spec/guest-memory.md](spec/guest-memory.md); bean `portps5-421p`).
    - [x] Direct-memory exports: real `sceKernelDirectMemoryQuery` extents and types, largest-free-run `sceKernelAvailableDirectMemorySize`, overflow and range hardening of allocate/release (`tests/memory/DirectMemoryExportTests.cpp`; [spec/guest-memory.md](spec/guest-memory.md)).
    - [x] Mapping placement and unmap: advisory address hints, `NO_OVERWRITE`, `sceKernelMunmap` over several mappings, holes and gaps (`tests/memory/MemoryMappingTests.cpp`, `GuestAllocationsUnmapTests.cpp`).
    - [x] `DirectMemory.hpp` uses the shared SCE error codes, pinned by literal-value tests (bean `portps5-mn0m`).
  - [x] Kernel error contract, timedwait, thread-self, and host TLS: verify POSIX/SCE errno families, `ETIMEDOUT` / `SCE_KERNEL_ERROR_ETIMEDOUT` conventions, and Win32 TLS isolation (`GuestKernelErrors`, `GuestCondTimedwait`, `GuestThreadSelf`, `HostThreadLocal`) ([spec/threading.md](spec/threading.md));
  - [ ] Shader recompiler: port instruction decoding bitfield validation, DPP swizzles, SDWA packing, 64-bit LDS, and divergent control-flow tests from Mesa ACO and KytyPS5 (`ShaderRecompilerComputeTests`, `shaderCfgTests`) ([spec/shader-recompiler.md](spec/shader-recompiler.md)). Only partly landed, so this was unticked on 2026-09-30: `RecompilerFixesTests` (saveexec, atomic-zero, LDS barriers, divergent regions), `RdnaPortedInstructionTests` (PR #60) and the synthetic golden corpus exist; no `ShaderRecompilerComputeTests`, `shaderCfgTests`, Mesa ACO or decoder `TEST_P` suites exist in the tree (bean `portps5-3asd` covers what landed and is completed; the remaining ports are bean `portps5-qxip`).
- [ ] Interim FMV correctness: AnyPS5 `main`'s Bink-plane write-back (merged PR #5) is ported as a general mechanism, *adjacent block-generation advance*, with no switch and no title reference. It serves M1–M2 and is replaced in M3 ([spec/video-fmv.md](spec/video-fmv.md)). Not implemented: there is no adjacent-generation code under `libSceAgcDriver/Graphics` on main (bean `portps5-ux18`).
- [ ] Recompiler: bindless tables with bounds taken from device limits ([spec/shader-recompiler.md](spec/shader-recompiler.md); bean `portps5-7li6`).
- [x] Relinker: make the existing `.eh_frame`-seeded CFG (`CodeInstructionCollector`) the only instruction-discovery engine, as one shared `CodeMap` replacing the linear sweep ([spec/relinker.md](spec/relinker.md)); support Linux load-alignment and `--windows-gui` PE subsystem switch.
- [x] Export ABI: the `APS5_EXPORT_FN` export macro, which declares an export `APS5_VABI` and `noexcept` ([spec/build-toolchain.md](spec/build-toolchain.md)). The macro exists; adopting it at call sites is the open item below.
- [ ] Guest memory: replace the arena's O(n) first-fit scan with a free-list allocator; add explicit pins, the registry-owned page-state table, and return codes in place of throws ([spec/guest-memory.md](spec/guest-memory.md)). Parts landed as unwired building blocks (bean `portps5-421p` tracks the wiring):
  - [x] extent-tree allocator core with a 10^6-operation differential test (PR #32, `GuestArenaExtent`);
  - [x] explicit pin tokens and the page-state table in `WriteWatchTracker` (PR #40);
  - [x] return codes instead of throws in direct memory and memory pools (PR #39);
  - [ ] the arena, heap spans and registry use them, and the runtime instantiates the tracker (bean `portps5-421p`).
- [x] Offline behaviour for network stack (`libSceNet`), AvPlayer playback state machine (`libSceAvPlayer`), NP/PSN, trophies, store, and user-service dialogs, so no gate title blocks on them at boot.
- [x] `libSceVideodec2` AVC decode (host H.264 decoder: FFmpeg built LGPL-only from the pinned `3rdparty/FFmpeg`, licence gate in [spec/build-toolchain.md](spec/build-toolchain.md)); HEVC and the AJM MP3 path stay open ([spec/video-fmv.md](spec/video-fmv.md)).
- [x] Rewrite pthread/umtx/cond on futex words (`WaitOnAddress`), with no global mutex and compact guest tids, and make errno returns correct. Unimplemented exports call `Unsupported()`, which logs and aborts ([spec/threading.md](spec/threading.md)).
- [ ] No host exception crosses an `APS5_VABI` boundary: adopt `APS5_EXPORT_FN` (`noexcept`) at every export and remove the remaining throwing exports such as `libSceSysmodule/Export.cpp` ([spec/threading.md](spec/threading.md) Error policy, [TechnicalDebt.md](TechnicalDebt.md) "Host exceptions"; bean `portps5-uanr`).
- [x] Image codecs: shared stb-backed JPEG/PNG layer (`core/Decoder`), `libSceJpegEnc` and `libScePngDec` ([spec/image-codecs.md](spec/image-codecs.md)); `libScePngEnc` remains open.
- [ ] Per-game TOML config, with `display.present_mode` and `display.resolution_scale` wired. Remove the `APS5_*` behaviour switches; keep a typed `[debug]` section (beans `portps5-c06p`, `portps5-dtwf`).
  - [x] `Config` schema, validation and the typed `[debug]` section (`libc/src/Config.cpp`, `core/libs/tests/Config.cpp`); no `APS5_` string literals remain in `core/` and the `policy` job enforces it;
  - [x] startup loads config for the `param.json` title ID before guest initializers (startup portion of bean `portps5-c06p`);
  - [ ] `display.present_mode` and `display.resolution_scale` reach the driver: the swapchain is hard-coded to FIFO (bean `portps5-dtwf`).
- [ ] Runtime telemetry: frame-time log, watchdog, structured logs, audio underrun and latency counters, and the A/V offset skeleton (`video_latency_ms`). The telemetry core, watchdog and mixer counter integration exist (PR #78, bean `portps5-f9a3` completed; [spec/verification.md](spec/verification.md) 4.3); the start-up, presenter and guest-progress call sites are not wired, so this stays open (bean `portps5-w1re`).
- [x] Hosted CI job `recompiler-golden` (synthetic corpus green in CI; merged into the `build_and_test` job by PR #38).
- [x] Hosted CI job `driver-lavapipe`: `ci.yml` job `driver_lavapipe` runs `ctest --preset lavapipe` on Mesa lavapipe (bean `portps5-ekx3`).
- [ ] Inventory each gate title's imports (NIDs, audio and video codecs, dialogs). One title of five is recorded; a repeatable tool (bean `portps5-zadg`) and the other four (bean `portps5-3eh1`) are open.
  - Dreaming Sarah (recorded 2026-09-29): 815 relocation refs / 484 unique NIDs, all 484/484 resolve at link time to built prx exports (6 libc locale/iostream data symbols resolve to stubs/host-backed placeholders); `libSceVideoOut` `Config::Loader` verbatim exports resolved. Detail in [spec/relinker.md](spec/relinker.md) Open questions.

**Exit criteria**
- [ ] (bean `portps5-0mv7`) With title-specific code removed, Demon's Souls reaches the in-engine intro cinematic, the stage AnyPS5 PR #5 reached. Its fill and copy kernels run as the title's own shaders, without replacement. The `policy` CI job is green.
- [x] Sync microbenchmark: uncontended lock/unlock at least 10× faster than the old implementation, and the pthread and `SyncOnAddress` GoogleTest suites pass.
- [ ] The hosted golden corpus contains synthetic shaders that cover every decoded instruction class, and it is green in CI. The local-only game-derived corpus replays with 0 validation failures (bean `portps5-oq4m`).

## Milestone 2: 2D gate (titles 1–2)

**Scope**
- [ ] Input: XInput, DualSense over USB, and keyboard/mouse mapping in TOML ([spec/input.md](spec/input.md); bean `portps5-de24`).
  - [x] SDL joystick/HIDAPI are enabled (`CMakeLists.txt:55-58`), and SDL game controllers feed `scePadRead` with hot-plug, slot assignment, a radial dead zone and keyboard merge (PRs #54 and #57, tests in `tests/input/ControllerInputTests.cpp`). Not verified with a physical controller;
  - [x] mouse backend (`libSceMouse/src/mouse_impl.cpp`) and VideoOut routing exist, but the `sceMouse*` exports are still `Unsupported()` stubs and `mouse_tests` is `DISABLED` (bean `portps5-afme`, low priority);
  - [x] DualSense output path in software: rumble, light bar, trigger effects, motion, touchpad fingers and a pure hot-plug `SlotTable`, with device-free GoogleTests (`tests/input/Pad*Tests.cpp`, bean `portps5-j7ds`). Not verified with a physical controller (bean `portps5-ds7h`);
  - [ ] TOML keyboard/mouse bindings (only `[input] deadzone` is consumed), XInput and DualSense USB on the manual matrix, slot reassignment tests with injected SDL events (bean `portps5-de24`).
- [x] Establish filesystem sandbox, input, and audio GoogleTest suites ported from ecosystem references:
  - [x] Filesystem sandbox: port path-traversal containment (`../`), mount sandbox isolation, and default-deny permission tests from SharpEMU (`KernelSandboxEscapeTests`) ([spec/save-data.md](spec/save-data.md));
  - [x] Save data: port directory layout, quota enforcement, atomic commit, and crash-safe snapshot restore tests;
  - [x] Input: port DualSense USB report parsing, radial deadzone calculation, rumble motor translation, and hotplug slot assignment tests from KytyPS5 (`PadHapticsTests`) ([spec/input.md](spec/input.md));
  - [x] Audio: port AudioOut2 port lifecycle, ATRAC9 header decoding, and mixer resampling tests from KytyPS5 (`AudioOut2PortTests`) ([spec/audio.md](spec/audio.md)).
- [x] Save data: dialogs return scripted and logged results instead of silent stubs; saves are stored per title, with crash-safe snapshots and a one-time copy of the old `_sd` layout ([spec/save-data.md](spec/save-data.md)).
- [x] Audio: a single host mixer with a resampler and a soft limiter, on one device clock ([spec/audio.md](spec/audio.md)).
- [ ] The disk pipeline cache (bean `portps5-8gdr`).
- [x] Driver: depth/stencil and conditional colour-write state decode, `VkPipelineDepthStencilStateCreateInfo` emission and pipeline-cache keying (`Graphics/src/State.cpp`).
- [x] Driver: host depth/stencil surface (`DB_Z_INFO` decode, host `VkImage`, render-pass attachment, `DB_RENDER_CONTROL` clears, depth bounds, `DB_DEPTH_CONTROL` bit 31 colour suppression) (`Graphics/src/DepthSurface.cpp`, [spec/gpu-driver.md](spec/gpu-driver.md); PR #55, bean `portps5-mij8` completed).
- [x] Per-title `/savedata0` mount for guest file I/O with sandboxed paths (PR #53, bean `portps5-10fr` completed). `sceKernelLseek` returns `EOVERFLOW` at or above 2 GiB; a 64-bit path is bean `portps5-65h0`.
- [ ] Driver: retile depth to guest memory (CPU or shader reads of a depth buffer) and guest-memory upload of never-cleared depth surfaces. A draw that depth-tests a surface that was never cleared is rejected until then (bean `portps5-9s7e`).
- [ ] `tools/regress` local regression plus results JSON upload, with the config hash and the "FMV played" rule (bean `portps5-3m3u`). Runner, results JSON and pass rule landed as `tools/regress.py`; frame checks, checkpoint replay, shader corpus step and upload remain, so this stays open.

**Exit criteria**
- [ ] Dreaming Sarah and TMNT: Shredder's Revenge pass the full-run protocol (average ≥30 fps, 1% low ≥20 fps, 1080p, 0 crashes and 0 softlocks, save round-trip). Dreaming Sarah: bean `portps5-kmb6`.
- [ ] Their results JSON is published, and the compatibility list is generated (TMNT full run bean `portps5-macl`; generator bean `portps5-4bkt`).

## Milestone 3: 3D core (title 3)

**Scope**
- [ ] Driver:
  - split into CommandProcessor, Recorder, Buffer/Texture/Pipeline caches, Rasterizer and Presenter, recording into a GPU IR with explicit resource states and a queue tag (bean `portps5-hkwd`; [spec/gpu-driver.md](spec/gpu-driver.md) Decision);
  - a GPU-side path for the `DRAW_INDIRECT` family (bean `portps5-y2k5`; AnyPS5 `main@75a8668` takes `vkCmdDraw[Indexed]Indirect[Count]` only when the record fold, shader path, draw index and memory state allow it, and otherwise reads records on the CPU: `Draw.hpp:43`, `Draw.cpp:823-830`);
  - general block-generation write tracking for GPU-written surfaces, replacing the interim adjacent block-generation advance from M1–M2. Tomb Raider and Bugsnax FMV depend on it (bean `portps5-c08m`);
  - redesign capture ordering: resolve buffers on the GPU, resolve images at submit time, and never satisfy a wait from an unexecuted label while a capture depends on it (bean `portps5-ubec`).
- [ ] Establish Vulkan driver cache and recompiler structurizer GoogleTest suites:
  - Driver resource caches: port buffer/texture cache overlap, staging ring-buffer exhaustion, and descriptor set lifecycle tests adapted from DXVK and RPCS3 patterns ([spec/gpu-driver.md](spec/gpu-driver.md); bean `portps5-oagr`);
  - Structurizer: recompiler fuzz corpus and synthetic unstructured control-flow graphs covering irreducible loops and goto-elimination fallbacks ([spec/shader-recompiler.md](spec/shader-recompiler.md)).
- [ ] Recompiler: goto-elimination structurizer fallback, and bounded hash-indexed variants (bean `portps5-g580`, which also owns the structurizer fuzz corpus).
- [ ] Save data: the multi-slot list dialog, for Tomb Raider save/load ([spec/save-data.md](spec/save-data.md); bean `portps5-uag4`).

**Exit criteria**
- [ ] Tomb Raider I-III Remastered passes the full-run protocol (bean `portps5-32ee`).
- [ ] A Demon's Souls stress run of 3 × 150 s of the intro cinematic has 0 wedges, with 0 skipped dispatches from the "guest memory is not readable" class (bean `portps5-273j`).
- [ ] Recompiler fuzz corpus: 0 structurizer throws (bean `portps5-g580`).

## Milestone 4: Middleware (title 4)

**Scope**
- [ ] UE4 job-system coverage (event flags, semaphores, fibers) (bean `portps5-mv7o`).
- [ ] Establish fiber and job-system GoogleTest suites (bean `portps5-k34n`):
  - Fibers and event flags: port fiber stack-switching, fiber-local storage (FLS), and event flag race perturbation tests from SharpEMU (`Fiber*Tests`) ([spec/threading.md](spec/threading.md));
  - Wave64 subgroup operations: synthetic compute dispatch validation for 64-wide lanes ([spec/shader-recompiler.md](spec/shader-recompiler.md)).
- [ ] Wave64 through `VK_EXT_subgroup_size_control` where supported (bean `portps5-cexw`).
- [ ] A GPU-side descriptor heap for bindless (`VK_EXT_descriptor_indexing` or `VK_EXT_descriptor_buffer`) (bean `portps5-zqk2`).
- [ ] Fill and copy kernels recognised by general IR patterns (bean `portps5-sio1`).

**Exit criteria**
- [ ] Bugsnax passes the full-run protocol (bean `portps5-td65`).
- [ ] Every `[workarounds]` key used by a gate title has an entry in `docs/workarounds.md` (bean `portps5-il6p`).

## Milestone 5: AAA (title 5)

**Scope**
- [ ] Demon's Souls from the first level to credits:
  - streaming and resource aliasing at full size (bean `portps5-951o`);
  - host-import budget sized automatically (bean `portps5-wvxm`; the AnyPS5 PR #5 description on GitHub reports a manual 16 GiB `APS5_HOST_IMPORT_MIB` override for later stages);
  - direct-memory aliasing (the same physical range mapped at several guest addresses) with write tracking, which `GetWriteWatch` may not cover ([spec/guest-memory.md](spec/guest-memory.md));
  - audio object-port panning ([spec/audio.md](spec/audio.md); bean `portps5-hwnz`).
- [ ] Direct-memory multi-mapping and host-import stress suites: perturbation tests covering concurrent aliased writes and memory tracker cache coherency under 16 GiB budget pressure (bean `portps5-9p2i`).
- [ ] Performance pass against the bar (bean `portps5-vj60`; performance track P3).
- [ ] Spike: llvm-mingw clang with PDBs, adopted only if the DWARF unwinder validates (bean `portps5-qf1m`).

**Exit criteria**
- [ ] Demon's Souls passes the full-run protocol, with title-specific behaviour only in its documented TOML (bean `portps5-clbd`).

## Milestone 6: 1.0 release

**Scope**
- [ ] A release full run of all five titles on the release commit (bean `portps5-vrfx`).
- [ ] Release docs: user guide (CLI usage, the legal boundary, the ban on sharing converted executables), `docs/workarounds.md`, and the compatibility list (bean `portps5-zl69`).
- [ ] Record the PRD R1 licence risk status in the release notes (bean `portps5-2t0e`).

**Exit criteria**
- [ ] 5/5 gate titles pass the full-run protocol on the release commit (bean `portps5-vrfx`).
- [ ] Hosted CI and local regression are green (bean `portps5-vrfx`).
- [ ] The release notes are published (bean `portps5-zl69`).

## Performance track (cross-cutting, M2–M5)

The track keeps the steady-state invariants of [PRD §4.5](PRD.md) true while the milestones land. It runs alongside M2–M5 and gates nothing by itself; the milestone exit criteria still do. Every step measures first: before/after numbers with the same build flags and the same run protocol. Epic bean `portps5-7fqk`.

**P0: Measure** (M2)
- [x] Frame-time log, stalls, warm-up and pipeline-creation counts (PRs #76, #78).
- [ ] Telemetry call sites wired: start-up, presenter, guest progress (bean `portps5-w1re`).
- [ ] Results JSON frame-time percentiles (p50, p90, p99) and `tools/regress.py compare` against a stored baseline (bean `portps5-rrll`).
- [ ] Per-frame breakdown: CPU record and submit, GPU time from Vulkan timestamp queries, CPU wait on GPU, present wait (bean `portps5-hfiw`).
- [ ] Violation counters for invariants P1, P2, P3 and P5 (bean `portps5-aifo`).
- [ ] Telemetry completeness: a crash event producer (`portps5-yhj7`); `run.start` with build preset, commit, device, driver, present mode and scale (`portps5-g2ll`); memory and VRAM peaks (`portps5-ybq3`); stall attribution (`portps5-26s5`); boot and load timing (`portps5-pk9m`); shader and pipeline compile times (`portps5-tg51`); numeric `dialog.open` (`portps5-7qon`); `prepare` copies `config/` (`portps5-3b4e`).
- [ ] Measurement tools: Tracy CPU and GPU zones behind `[debug] profile` (`portps5-wba0`), perf scenes per gate title (`portps5-52bs`), CPU microbenchmarks with a nightly trend (`portps5-i58o`).

**P1: Per-draw CPU cost** (M2–M3, invariants P2 and P5)
- [x] Pooled images for resident render-target sampling (PR #74, bean `portps5-r7qk` step 1).
- [x] Frame-timing report only when `debug.profile` has `gpu` (PR #71).
- [ ] SSE2 compare of cached textures (PR #75) and the write-tracker skip of unchanged textures (PR #77; it takes effect once bean `portps5-421p` wires the tracker). The P5 violation itself is bean `portps5-2k6d`.
- [ ] Resident render-target baseline, then decide step 2 (bean `portps5-r7qk`).

**P2: GPU-side resolution** (M3, invariants P1 and P6). The work is the M3 driver scope: the Recorder in the submit path (bean `portps5-tiod`), the GPU path for the indirect family, block-generation tracking and the capture-ordering redesign. The M3 module split records into a GPU IR with explicit resource states ([spec/gpu-driver.md](spec/gpu-driver.md) Decision), so barrier optimization, multithreaded recording and frame overlap can come later without a rewrite.
- [ ] M3 exit adds: in the Tomb Raider regression result, the P1 and P2 counters are 0 in steady state, or each nonzero counter has a bean (bean `portps5-32ee`).

**P3: Performance pass** (M5). This is the existing M5 item. Every remaining invariant violation is removed or justified in its owner spec, and these open questions are decided with data: the async-compute queue and HTILE/DCC ([spec/gpu-driver.md](spec/gpu-driver.md) 2–3), multithreaded PM4 recording (gpu-driver 11), pipeline libraries ([spec/pipeline-cache.md](spec/pipeline-cache.md) 3), and host core placement ([spec/threading.md](spec/threading.md) 9).

**P4: Compiler and host** (M2–M5, invariants P2, P3 and GPU time). Release builds ship without the SPIRV-Tools optimizer (PRD R1), so shader quality depends on our own passes.
- [ ] Shader optimization: GVN/CSE, copy propagation and control-flow simplification (`portps5-t5qa`), uniformity analysis (`portps5-1uym`), specialization constants for draw state (`portps5-9gsx`), and a spike on running `spirv-opt` out of process (`portps5-m5n7`).
- [ ] Pipeline compilation off the submit thread with graphics pipeline library fast-link, background optimized relink and parallel warm-up; no draw is ever skipped (`portps5-c1oi`).
- [ ] Driver: upload engine on timeline semaphores (`portps5-dtiq`), barrier and redundant-state pass on the GPU IR (`portps5-p2qp`), frame pacing with present_wait (`portps5-jy7n`).
- [ ] Host: `x86-64-v3` with `-ffp-contract=off` and LTO (`portps5-8x1k`), GCC PGO from perf scenes (`portps5-f2r1`), guest allocator measured before any change (`portps5-03bi`).
- [ ] Large pages for the guest arena and GPU allocations: open question, decided with data in the M5 pass ([spec/guest-memory.md](spec/guest-memory.md)).
Upscalers, frame generation and HDR are 2.0 (M12). 60/120 fps targets and frame-rate unlock patches are beyond 2.0 (PRD §11).

### Parallel lanes

A lane can start when its blockers are done. Lanes in the same row can run in parallel. The beans record the same edges (`blocked_by`), so `beans list --ready` shows what can start now.

| Lane | Beans | Blocked by |
|---|---|---|
| Config copy at conversion | `portps5-c06p` (startup call landed in PR #71) | none |
| Results compare | `portps5-rrll` | none |
| Tracker wiring | `portps5-421p` | none |
| Recorder in submit path | `portps5-tiod` | none |
| Telemetry call sites | `portps5-w1re` | none (config startup landed in PR #71) |
| Display keys | `portps5-dtwf` | none (config startup landed in PR #71) |
| Frame breakdown | `portps5-hfiw` | `portps5-w1re`, `portps5-tiod` |
| Violation counters | `portps5-aifo` | `portps5-w1re` |
| Resident RT step 2 | `portps5-r7qk` | `portps5-w1re` (baseline needs telemetry) |
| Tracy profiler | `portps5-wba0` | none |
| Build flags | `portps5-8x1k` | none |
| Microbenchmarks | `portps5-i58o` | none |
| Shader passes | `portps5-t5qa`, `portps5-1uym` | none |
| spirv-opt spike | `portps5-m5n7` | none |
| Regress config copy | `portps5-3b4e` | none |
| Perf scenes | `portps5-52bs` | `portps5-3m3u` |
| Specialization constants | `portps5-9gsx` | `portps5-8gdr` |
| Async pipeline compile | `portps5-c1oi` | `portps5-8gdr` |
| Compile-time telemetry, crash, boot, dialogs | `portps5-tg51`, `portps5-yhj7`, `portps5-pk9m`, `portps5-7qon` | `portps5-w1re` |
| Run metadata | `portps5-g2ll` | `portps5-w1re` |
| Memory telemetry | `portps5-ybq3` | `portps5-w1re`, `portps5-l77s` |
| Frame pacing | `portps5-jy7n` | `portps5-dtwf`, `portps5-w1re` |
| Upload engine | `portps5-dtiq` | `portps5-hkwd` |
| Barrier pass | `portps5-p2qp` | `portps5-hkwd`, `portps5-hfiw` |
| Stall attribution | `portps5-26s5` | `portps5-hfiw`, `portps5-aifo` |
| Allocator | `portps5-03bi` | `portps5-wba0` |
| PGO | `portps5-f2r1` | `portps5-52bs`, `portps5-8x1k` |
| P5 texture revalidation | `portps5-2k6d` | `portps5-421p` |

## v2 seams in v1 (cross-cutting, M2–M5)

Design for 2.0, implement for 1.0. A seam moves into v1 only when v1 code uses it, it is cheap now, and the 2.0 milestone that plugs into it is named. Nothing here adds a 2.0 feature to 1.0. Epic bean `portps5-epoi`.

| Seam | v1 implements | 2.0 plugs in | Milestone | Bean |
|---|---|---|---|---|
| Host platform layer `core/host/` and a Win32 allowlist that only shrinks | Win32 backend for futex, clock, virtual memory; contract tests | Linux backend (M7) | M2–M3 | `portps5-37j0` |
| Proton smoke test | best effort, never blocks a PR | Linux validation (M7) | M2 | `portps5-qfac` |
| GPU IR with resource states and a queue tag | one queue, single-threaded recording | async compute, multithreaded recording, RT passes (M9–M10) | M3 | `portps5-hkwd` |
| Residency interface on the driver caches | no budget pressure, simple LRU | VRAM oversubscription manager (M8) | M3 | `portps5-hps3` |
| Guest memory registry: physical allocation with N views | 1 view, plus a synthetic 2-view test | aliasing at full size (M5), streaming (M8) | M3 | `portps5-gkef` |
| One device capability table | subgroup size, descriptor model | ray tracing, mesh shaders (M9) | M3 | `portps5-l77s` |
| Recompiler decodes RT and NGG/mesh ops into IR | `Unsupported()` with a log | RT and mesh lowering (M9) | M4 | `portps5-jehk` |
| Presenter composition hook and swapchain colour-space selection | final pass is a copy; SDR swapchain | spatial upscale, HDR output, frame generation (M12) | M3 | `portps5-71o2` |
| Pipeline cache `EnvKey` hashes enabled features | current stages | new stages without a format break | M2 | `portps5-8gdr` |
| Positional file I/O and a request queue | `sceKernelAio*` | streaming and decompression pipeline (M8) | M3 | `portps5-j4e1` |

Two spikes de-risk 2.0 and can run any time, in parallel with everything: guest TLS on Linux (`portps5-u16x`) and the guest RT BVH layout against Mesa RADV (`portps5-mdu8`).

### Parallel lanes (seams)

| Lane | Bean | Blocked by |
|---|---|---|
| Host platform layer | `portps5-37j0` | none |
| Proton smoke test | `portps5-qfac` | none |
| Capability table | `portps5-l77s` | none |
| RT and NGG decode | `portps5-jehk` | none |
| TLS spike | `portps5-u16x` | none |
| BVH spike | `portps5-mdu8` | none |
| Registry N views | `portps5-gkef` | `portps5-421p` |
| GPU IR split | `portps5-hkwd` | `portps5-tiod` |
| Residency interface | `portps5-hps3` | `portps5-hkwd` |
| Direct-memory aliasing (M5) | `portps5-r2ns` | `portps5-gkef` |
| Positional file I/O and Aio | `portps5-j4e1` | `portps5-37j0` |
| Presenter composition hook | `portps5-71o2` | `portps5-l77s` |

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
| F9 telemetry | M1 (core, PR #78), M2 (call sites `portps5-w1re`, completeness beans in the performance track P0) | Results JSON present for every run |
| Performance bar (§4.3) | M1 (sync), M2 (depth/stencil), M3 (driver), M4 (subgroups), M5 (perf pass) | Results JSON fps stats |
| Performance invariants (§4.5) | Performance track P0–P3 (M2–M5) | Results JSON violation counters, before/after measurements |
| Ecosystem test suites (GTest, Kyty, SharpEMU, FreeBSD, Mesa, Wine) | M0 (framework), M1 (core runtime), M2 (sandbox/input/audio), M3 (caches/structurizer), M4 (fibers/jobs) | CI `unit`, `recompiler-golden`, `driver-lavapipe` |
| Gate 1: Dreaming Sarah | M2 | Full run |
| Gate 2: TMNT: Shredder's Revenge | M2 | Full run |
| Gate 3: Tomb Raider I-III Remastered | M3 | Full run |
| Gate 4: Bugsnax | M4 | Full run |
| Gate 5: Demon's Souls | M5 | Full run |
| No title-specific code | M1 (policy), all milestones | `policy` CI job |
| Hosted CI without GPU; local results | M0, M1, M2 | Verification spec |
| R1 licence risk documented | PRD §7, M6 release notes | Doc review |

# Part II: 2.0 (draft)

2.0 objective and gates: [PRD §10](PRD.md). Part II starts after M6. Before that only the Part I seams and the two spikes run. Each milestone becomes its own `ooo seed`, and its beans are created then, with `blocked_by` links. A milestone starts only when the previous one's exit criteria pass. The exceptions are M8, M9 and M12, which may run in parallel once M7 exits: they touch different subsystems (memory and I/O, GPU features, presentation). M8 and M9 have separate gate titles; M12 has no gate title and does not block M11, but it must land before the 2.0 release.

```
M6 (1.0) ─▶ M7 platform + tier ─┬─▶ M8 streaming (gate 6) ─┬─▶ M10 scale (gate 8) ─▶ M11 GTA VI (gate 9)
                                ├─▶ M9 RT + geometry (gate 7) ┘
                                └─▶ M12 PC enhancement layer (no gate; ships with 2.0)
```

## Milestone 7: Native Linux and the 2.0 tier

**Scope**
- [ ] Linux backend for every host platform service ([spec/host-platform.md](spec/host-platform.md)), relinker ELF output built and tested in CI, and a hosted Linux build running the same `ctest` suites.
- [ ] Guest TLS on Linux per the spike result (`portps5-u16x`).
- [ ] The 2.0 reference tier: benchmark floors set from the 1.0 M5 performance-pass data, with ray tracing required (PRD §10.3).
- [ ] Pin every 2.0 gate title that can be dumped (PRD §10.1).

**Exit criteria**
- [ ] The five 1.0 gate titles pass their regression on Linux with the 1.0 pass rules.
- [ ] The Win32 allowlist outside `core/host/` is empty.

## Milestone 8: Streaming and residency (gate 6, Horizon Forbidden West)

**Scope**
- [ ] Residency manager behind the driver-cache interface (`portps5-hps3`): a VRAM budget from `VK_EXT_memory_budget`, eviction and re-upload, no eviction inside a frame's working set.
- [ ] Hardware-decompression requests served off the guest thread on the positional I/O queue (PRD V3, licence recorded per V-R4).
- [ ] Guest memory beyond host VRAM at full open-world size, with write tracking and aliasing (M5 mechanisms at scale).

**Exit criteria**
- [ ] Horizon Forbidden West passes the full-run protocol on the 2.0 tier, with 0 stalls caused by eviction.

## Milestone 9: Ray tracing and modern geometry (gate 7, Ratchet & Clank: Rift Apart)

**Scope**
- [ ] Guest acceleration-structure builds and ray queries on Vulkan ray tracing, designed from the BVH spike (`portps5-mdu8`).
- [ ] Primitive (NGG) and mesh shader stages in the recompiler and driver, on the IR ops from `portps5-jehk`.
- [ ] Async compute on a separate host queue through the GPU IR queue tag (gpu-driver open question 3).

**Exit criteria**
- [ ] Ratchet & Clank: Rift Apart passes the full-run protocol on the 2.0 tier in one of its RT modes. Skipping RT work is a fail.

## Milestone 10: Open-world scale (gate 8, Marvel's Spider-Man 2)

**Scope**
- [ ] Multithreaded recording and bounded CPU/GPU frame overlap (invariant P6, gpu-driver open question 11), decided from frame-breakdown data.
- [ ] Every remaining PRD §4.5 violation on gates 6–8 removed or justified.

**Exit criteria**
- [ ] Marvel's Spider-Man 2 passes the full-run protocol on the 2.0 tier.

## Milestone 11: 2.0 release (gate 9, Grand Theft Auto VI)

**Scope**
- [ ] Pin GTA VI once a user-owned dump exists (PRD V-R1); inventory its imports.
- [ ] Release full runs of gates 6–9 on Windows and Linux.

**Exit criteria**
- [ ] GTA VI passes the full-run protocol on the 2.0 tier at ≥ 30 fps average and ≥ 20 fps 1% low, with title-specific behaviour only in its documented TOML.
- [ ] Gates 6–8 still pass, on both platforms.
## Milestone 12: PC enhancement layer (parallel after M7, ships with 2.0)

All enhancements are opt-in per title, configured through documented `[enhancement]` keys that name the mechanism, and switched off for every pass measurement (PRD V7).

**Scope**
- [ ] Spatial upscaling (FSR1/CAS, MIT) in the presenter's composition pass (`portps5-71o2`), with internal resolution from `display.resolution_scale`.
- [ ] HDR output: the guest VideoOut HDR mode mapped to an HDR10 or scRGB swapchain with `VK_EXT_hdr_metadata`, chosen from the capability table.
- [ ] Temporal upscaling (FSR2+; DLSS and XeSS only if the licence spike `portps5-f4bl` clears them), fed by the general input recogniser from spike `portps5-8ov1`.
- [ ] Frame generation, presentation only: it never changes guest simulation, input or timing, and it is off while telemetry measures a pass.
- [ ] Experimental flip-rate override (`[enhancement] flip_rate_override`), off by default, never part of a pass, with the timing risk documented (PRD §11).

**Exit criteria**
- [ ] Each enhancement runs on at least two 2.0 gate titles with no change to their pass results when switched off.
- [ ] `docs/workarounds.md` (or a new `docs/enhancements.md`) lists every `[enhancement]` key with its mechanism.
# Part III: beyond 2.0 (unscheduled)

v3 candidates and permanent exclusions, with the reason each is out, are in [PRD §11](PRD.md). The AI-assisted optimization research there starts with deterministic shader-variant autotuning and profile-guided relinker layout, and keeps every model output as a verified, locally derived cache.

## Traceability (2.0)

| 2.0 goal (PRD §10) | Delivered in | Seam from Part I |
|---|---|---|
| V1 native Linux | M7 | `portps5-37j0`, `portps5-u16x` |
| V2 streaming and residency | M8 | `portps5-hps3`, `portps5-gkef` |
| V3 decompression | M8 | `portps5-j4e1` |
| V4 ray tracing | M9 | `portps5-l77s`, `portps5-jehk`, `portps5-mdu8` |
| V5 modern geometry | M9 | `portps5-jehk` |
| V6 async compute, frame overlap | M9, M10 | `portps5-hkwd`, performance track |
| V7 PC enhancement layer | M12 | `portps5-71o2`, `portps5-l77s`, `portps5-hkwd` |
| Gate 9 GTA VI at 30 fps | M11 | all of the above |
