# PortPS5 — Roadmap to 1.0

Status: draft v1 · 2026-09-27

This roadmap is phased, with no calendar dates. Capacity is a solo maintainer plus AI agents, part-time. Each milestone has measurable exit criteria and unlocks gate titles in order of risk. A milestone starts only when the previous one's exit criteria pass. Each milestone becomes its own `ooo seed`.

## Milestone 0: Fork foundation

**Scope**
- Rebase `yuriolive/PortPS5` onto AnyPS5 `main` history (`e06dbff`), and credit AnyPS5 in the README.
- Keep the GPL-2.0-only licence.
- Change the build: bump to C++23, add CMakePresets, pin the MinGW GCC 15.2 toolchain, and wire every existing test into `ctest`.
- Replace the no-comments rule in CONVENTIONS with "comment why, not what".
- Hosted CI jobs `build`, `unit` and `policy` ([spec/verification.md](spec/verification.md) §1).
- Dump the five gate titles and record their pins in the PRD §4.1 table. Swap any PS4-only build within the same tier (PRD R4).

**Exit criteria**
- `main` builds from a clean clone in CI, and `ctest` is green.
- The PRD pin table has no TBD cells.
- Baseline preserved. At the start of M0, run Demon's Souls on upstream `main` (`e06dbff`) and record the furthest observable stage (for example "SIE logo presented"). After the rebase, the fork reaches that same stage.

## Milestone 1: Selective PR #5 port and runtime core

**Scope**
- Port from PR #5 onto the fork, without its title-specific code:
  - the Recorder;
  - host import with a staging fallback;
  - GPU detile;
  - GuestArena/GuestHeap behind `IWriteTracker`;
  - `--to-intel` SSE4a lowering;
  - AudioOut2 and ATRAC9;
  - the recompiler fixes (saveexec order, atomic-zero, LDS barriers);
  - the `agc_shader_replay` tool and request serialisation.
- Offline behaviour for NP/PSN, trophies, store and user-service dialogs, so no gate title blocks on them at boot.
- Rewrite pthread/umtx/cond on futex words (`WaitOnAddress`), with no global mutex, and make errno returns correct.
- Per-game TOML config. Remove the `APS5_*` behaviour switches; keep a typed `[debug]` section.
- Runtime telemetry: frame-time log, watchdog, structured logs.
- Hosted CI jobs `recompiler-golden` and `driver-lavapipe`.
- Inventory each gate title's imports (NIDs, audio and video codecs, dialogs).

**Exit criteria**
- With title-specific code removed, Demon's Souls reaches the in-engine intro cinematic, the stage PR #5 reached. Its fill and copy kernels run as the title's own shaders, without replacement. The `policy` CI job is green.
- Sync microbenchmark: uncontended lock/unlock at least 10× faster than the old implementation, and the pthread tests pass.
- The hosted golden corpus contains synthetic shaders that cover every decoded instruction class, and it is green in CI. The local-only game-derived corpus replays with 0 validation failures.

## Milestone 2: 2D gate (titles 1–2)

**Scope**
- Input: XInput, DualSense over USB, keyboard/mouse mapping in TOML.
- Save data: dialogs return scripted and logged results instead of silent stubs; saves are stored per title.
- The disk pipeline cache.
- Driver: depth/stencil and conditional colour-write state (`State.cpp:318-322` currently rejects them), because 2D engines also set them.
- `tools/regress` local regression plus results JSON upload.

**Exit criteria**
- Dreaming Sarah and TMNT: Shredder's Revenge pass the full-run protocol (average ≥30 fps, 1% low ≥20 fps, 1080p, 0 crashes and 0 softlocks, save round-trip).
- Their results JSON is published, and the compatibility list is generated.

## Milestone 3: 3D core (title 3)

**Scope**
- Driver:
  - split into CommandProcessor, Recorder, Buffer/Texture/Pipeline caches, Rasterizer and Presenter;
  - the `DRAW_INDIRECT` family;
  - general block-generation write tracking for GPU-written surfaces, replacing PR #5's Bink-plane "adjacent-generation" special case. Tomb Raider and Bugsnax FMV depend on it;
  - redesign capture ordering: resolve buffers on the GPU, resolve images at submit time, and never satisfy a wait from an unexecuted label while a capture depends on it.
- Recompiler: goto-elimination structurizer fallback, and bounded hash-indexed variants.

**Exit criteria**
- Tomb Raider I-III Remastered passes the full-run protocol.
- A Demon's Souls stress run of 3 × 150 s of the intro cinematic has 0 wedges, with 0 skipped dispatches from the "guest memory is not readable" class.
- Recompiler fuzz corpus: 0 structurizer throws.

## Milestone 4: Middleware (title 4)

**Scope**
- UE4 job-system coverage (event flags, semaphores, fibers).
- Wave64 through `VK_EXT_subgroup_size_control` where supported.
- A GPU-side descriptor heap for bindless (`VK_EXT_descriptor_indexing` or `VK_EXT_descriptor_buffer`).
- Fill and copy kernels recognised by general IR patterns.

**Exit criteria**
- Bugsnax passes the full-run protocol.
- Every `[workarounds]` key used by a gate title has an entry in `docs/workarounds.md`.

## Milestone 5: AAA (title 5)

**Scope**
- Demon's Souls from the first level to credits:
  - streaming and resource aliasing at full size;
  - host-import budget sized automatically (the PR #5 description reports a manual 16 GiB override for later stages).
- Performance pass against the bar.
- Spike: llvm-mingw clang with PDBs, adopted only if the DWARF unwinder validates.

**Exit criteria**
- Demon's Souls passes the full-run protocol, with title-specific behaviour only in its documented TOML.

## Milestone 6: 1.0 release

**Scope**
- A release full run of all five titles on the release commit.
- Release docs: user guide (CLI usage, the legal boundary, the ban on sharing converted executables), `docs/workarounds.md`, and the compatibility list.
- Record the PRD R1 licence risk status in the release notes.

**Exit criteria**
- 5/5 gate titles pass the full-run protocol on the release commit.
- Hosted CI and local regression are green.
- The release notes are published.

## Traceability

| 1.0 goal (PRD) | Delivered in | Verified by |
|---|---|---|
| F1 local CLI conversion | M0 (existing), M1 (`--to-intel`) | CI build, M2 full runs |
| F2 save/load | M2 | Save round-trip in regression and full run |
| F3 audio | M1 (AudioOut2, ATRAC9), M1 codec inventory | Full runs |
| F4 FMV | M1 (saveexec fix), M3 (general block tracking) | FMV frame checks, A/V offset telemetry, full runs |
| F5 input (XInput, DualSense, keyboard) | M2 | Full runs |
| F6 per-game TOML | M1 | `policy` CI job, `docs/workarounds.md` |
| F7 disk pipeline cache | M2 | Warm-cache full runs |
| F8 offline PSN/trophies | M1 | Full runs never blocked |
| F9 telemetry | M1 | Results JSON present for every run |
| Performance bar (§4.3) | M1 (sync), M2 (depth/stencil), M3 (driver), M4 (subgroups), M5 (perf pass) | Results JSON fps stats |
| Gate 1: Dreaming Sarah | M2 | Full run |
| Gate 2: TMNT: Shredder's Revenge | M2 | Full run |
| Gate 3: Tomb Raider I-III Remastered | M3 | Full run |
| Gate 4: Bugsnax | M4 | Full run |
| Gate 5: Demon's Souls | M5 | Full run |
| No title-specific code | M1 (policy), all milestones | `policy` CI job |
| Hosted CI without GPU; local results | M0, M1, M2 | Verification spec |
| R1 licence risk documented | PRD §7, M6 release notes | Doc review |
