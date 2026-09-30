# PortPS5 — Roadmap to 1.0

Status: draft v1 · 2026-09-27

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
- [ ] Baseline preserved. At the start of M0, run Demon's Souls on upstream `main` (`e06dbff`) and record the furthest observable stage (for example "SIE logo presented"). After the rebase, the fork reaches that same stage.

## Milestone 1: Selective PR #5 port and runtime core

**Scope**
- [ ] Port from PR #5 onto the fork, without its title-specific code:
  - [ ] the Recorder;
  - [ ] host import with a staging fallback;
  - [x] GPU detile;
  - [ ] GuestArena/GuestHeap behind `IWriteTracker`;
  - [x] `--to-intel` SSE4a lowering;
  - [x] AudioOut2 and ATRAC9;
  - [x] the recompiler fixes (saveexec order, atomic-zero, LDS barriers);
  - [ ] the `agc_shader_replay` tool and request serialisation.
- [ ] Establish foundational subsystem GoogleTest suites ported and adapted from open-source ecosystem references:
  - [x] Kernel synchronization & threading: port futex/umtx, pthread mutex/condvar/rwlock priority, and `WaitOnAddress` race perturbation tests from FreeBSD 12, Wine, and KytyPS5 (`SyncOnAddressTests`) ([spec/threading.md](spec/threading.md));
  - [x] Event queues: port kqueue/kevent edge/level triggers, user events, and timeout cancellation tests from FreeBSD 12 and KytyPS5 (`EventQueueLifetimeTests`);
  - [ ] Virtual memory ([spec/guest-memory.md](spec/guest-memory.md)):
    - [ ] port 16 KB page rounding, direct memory mapping and protect state transitions from FreeBSD 12 and KytyPS5 (`VirtualMemoryAllocationTests`: 241 of Kyty's 3,173 lines ported so far);
    - [x] port the memory-tracking behaviour from KytyPS5 `MemoryTrackerTests` onto `GuestMemoryTracking::Watch` (`tests/memory/MemoryTrackerTests.cpp`: range validation, page rounding, protection/fault resolution, invalidate, resolver contract, concurrency). Kyty's CPU/GPU dirty-ownership, upload/download ranges, `RangeSet` and region-mask batching have no counterpart until the `IWriteTracker` implementation lands, so they are not ported;
    - [ ] `IWriteTracker` write-watch tests (`Collect`, `MarkWritten`, generations, aliased views): blocked on the real tracker; only `NullTracker` exists.
  - [x] Kernel error contract, timedwait, thread-self, and host TLS: verify POSIX/SCE errno families, `ETIMEDOUT` / `SCE_KERNEL_ERROR_ETIMEDOUT` conventions, and Win32 TLS isolation (`GuestKernelErrors`, `GuestCondTimedwait`, `GuestThreadSelf`, `HostThreadLocal`) ([spec/threading.md](spec/threading.md));
  - [x] Shader recompiler: port instruction decoding bitfield validation, DPP swizzles, SDWA packing, 64-bit LDS, and divergent control-flow tests from Mesa ACO and KytyPS5 (`ShaderRecompilerComputeTests`, `shaderCfgTests`, `RecompilerFixesTests`) ([spec/shader-recompiler.md](spec/shader-recompiler.md)).
- [ ] Interim FMV correctness: PR #5's Bink-plane write-back is ported as a general mechanism, *adjacent block-generation advance*, with no switch and no title reference. It serves M1–M2 and is replaced in M3 ([spec/video-fmv.md](spec/video-fmv.md)).
- [ ] Recompiler: bindless tables with bounds taken from device limits ([spec/shader-recompiler.md](spec/shader-recompiler.md)).
- [x] Relinker: make the existing `.eh_frame`-seeded CFG (`CodeInstructionCollector`) the only instruction-discovery engine, as one shared `CodeMap` replacing the linear sweep ([spec/relinker.md](spec/relinker.md)); support Linux load-alignment and `--windows-gui` PE subsystem switch.
- [x] Export ABI: the `APS5_EXPORT_FN` export macro, which declares every export `APS5_VABI` and `noexcept` ([spec/build-toolchain.md](spec/build-toolchain.md)).
- [ ] Guest memory: replace the arena's O(n) first-fit scan with a free-list allocator; add explicit pins, the registry-owned page-state table, and return codes in place of throws ([spec/guest-memory.md](spec/guest-memory.md)).
- [x] Offline behaviour for network stack (`libSceNet`), AvPlayer playback state machine (`libSceAvPlayer`), NP/PSN, trophies, store, and user-service dialogs, so no gate title blocks on them at boot.
- [x] Rewrite pthread/umtx/cond on futex words (`WaitOnAddress`), with no global mutex and compact guest tids, and make errno returns correct. Unimplemented exports call `Unsupported()`, which logs and aborts; no throw crosses the `APS5_VABI` boundary ([spec/threading.md](spec/threading.md)).
- [ ] Per-game TOML config, with `display.present_mode` and `display.resolution_scale` wired. Remove the `APS5_*` behaviour switches; keep a typed `[debug]` section.
- [ ] Runtime telemetry: frame-time log, watchdog, structured logs, audio underrun and latency counters, and the A/V offset skeleton (`video_latency_ms`).
- [x] Hosted CI job `recompiler-golden` (synthetic corpus green in CI).
- [ ] Hosted CI job `driver-lavapipe`.
- [ ] Inventory each gate title's imports (NIDs, audio and video codecs, dialogs).
  - Dreaming Sarah (recorded 2026-09-29): 815 relocation refs / 484 unique NIDs, 478 resolve to built prx exports; 6 missing (all libc locale/iostream data); boot blocked at prx load (`libSceVideoOut` importing `Config::Loader` from `libc.prx`, `GetLastError` 127). Detail in [spec/relinker.md](spec/relinker.md) Open questions.

**Exit criteria**
- [ ] With title-specific code removed, Demon's Souls reaches the in-engine intro cinematic, the stage PR #5 reached. Its fill and copy kernels run as the title's own shaders, without replacement. The `policy` CI job is green.
- [x] Sync microbenchmark: uncontended lock/unlock at least 10× faster than the old implementation, and the pthread and `SyncOnAddress` GoogleTest suites pass.
- [ ] The hosted golden corpus contains synthetic shaders that cover every decoded instruction class, and it is green in CI. The local-only game-derived corpus replays with 0 validation failures.

## Milestone 2: 2D gate (titles 1–2)

**Scope**
- [ ] Input: XInput, DualSense over USB, and keyboard/mouse mapping in TOML. Foundation landed: mouse backend (`libSceMouse`), VideoOut input routing, and `mouse_tests` API pinning. XInput and DualSense are new work: enable SDL joystick/HIDAPI (currently off) and implement the `libScePad` controller paths, with hot-plug and slot assignment ([spec/input.md](spec/input.md)).
- [ ] Establish filesystem sandbox, input, and audio GoogleTest suites ported from ecosystem references:
  - [x] Filesystem sandbox: port path-traversal containment (`../`), mount sandbox isolation, and default-deny permission tests from SharpEMU (`KernelSandboxEscapeTests`) ([spec/save-data.md](spec/save-data.md));
  - [x] Save data: port directory layout, quota enforcement, atomic commit, and crash-safe snapshot restore tests;
  - [x] Input: port DualSense USB report parsing, radial deadzone calculation, rumble motor translation, and hotplug slot assignment tests from KytyPS5 (`PadHapticsTests`) ([spec/input.md](spec/input.md));
  - [ ] Audio: port AudioOut2 port lifecycle, ATRAC9 header decoding, and mixer resampling tests from KytyPS5 (`AudioOut2PortTests`) ([spec/audio.md](spec/audio.md)).
- [x] Save data: dialogs return scripted and logged results instead of silent stubs; saves are stored per title, with crash-safe snapshots and a one-time copy of the old `_sd` layout ([spec/save-data.md](spec/save-data.md)).
- [ ] Audio: a single host mixer with a resampler and a soft limiter, on one device clock ([spec/audio.md](spec/audio.md)).
- [ ] The disk pipeline cache.
- [ ] Driver: depth/stencil and conditional colour-write state, because 2D engines also set them. They are currently rejected at `State.cpp:152` on `main` and `State.cpp:318-322` in PR #5.
- [ ] `tools/regress` local regression plus results JSON upload, with the config hash and the "FMV played" rule.

**Exit criteria**
- [ ] Dreaming Sarah and TMNT: Shredder's Revenge pass the full-run protocol (average ≥30 fps, 1% low ≥20 fps, 1080p, 0 crashes and 0 softlocks, save round-trip).
- [ ] Their results JSON is published, and the compatibility list is generated.

## Milestone 3: 3D core (title 3)

**Scope**
- [ ] Driver:
  - split into CommandProcessor, Recorder, Buffer/Texture/Pipeline caches, Rasterizer and Presenter;
  - a GPU-side path for the `DRAW_INDIRECT` family (PR #5 implements these draws only by reading records on the CPU);
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
  - host-import budget sized automatically (the PR #5 description reports a manual 16 GiB override for later stages);
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
