# PortPS5 — Technical Specification

Status: draft v2 · 2026-09-27 · synced with `main` 2026-09-30

This folder holds the technical specification for PortPS5 1.0, one file per subsystem. Product goals and the 1.0 bar live in [../PRD.md](../PRD.md), and milestones in [../ROADMAP.md](../ROADMAP.md).

## Baseline

The spec starts from two trees:

- AnyPS5 `main@e06dbff`, the tree before PR #5 merged, about 83k lines of C/C++;
- AnyPS5 `main@75a8668`, the current tree, about 148k lines of C/C++ (`3rdparty/` excluded). It includes merged PR #5 (merge commit `7656458`; this spec first compared PR #5 at its commit `29b4601`) and 221 commits made after that merge.

File references in each subsystem spec are relative to the AnyPS5 tree (`core/...`) and say which tree they come from.

## Execution model

The guest's x86-64 code is not recompiled. The relinker rewrites the decrypted ELF into a Windows PE image. Replacement system libraries (`.prx`, built as shared libraries with NID-patched exports) implement Sony's APIs from public documentation and reverse-engineered behaviour. Host exports use the System V calling convention through `APS5_VABI` (`__attribute__((sysv_abi))`), so calls between guest and host need no thunks. GPU work is translated to Vulkan.

## Target architecture by version

```
PS5 game (user's own decrypted dump, stays local)
  │
  ▼
Relinker: ELF → PE (v1) · ELF output for Linux (v2) ............ relinker.md
  │  native x86-64, no CPU translation (never)
  ▼
PS5 runtime / HLE: libc, kernel, threading, files, audio (v1) ... libc, threading, guest-memory, audio, save-data
  │  host platform layer core/host/: Win32 (v1), Linux (v2) ...... host-platform.md
  ▼
AGC / PM4 command processor (v1) ................................ gpu-driver.md
  ▼
GPU IR: resource states, queue tag, draws, dispatches (v1, M3)
  │  descriptors heap (v1, M4) · barrier/state pass (v1, M3–M5)
  │  multithreaded recording, async compute (v2, M9–M10)
  ▼
Shader recompiler: RDNA2 → SSA IR → SPIR-V (v1) ................. shader-recompiler.md
  │  native optimizer passes (v1, M4–M5) · RT and mesh lowering (v2, M9)
  ▼
Vulkan 1.3 (v1) · RT, mesh, memory budget (v2) .................. gpu-driver.md, pipeline-cache.md
  ▼
Presenter: composition hook (v1 seam) ........................... gpu-driver.md
  │  frame pacing, FIFO-relaxed/mailbox (v1)
  │  spatial/temporal upscale, frame generation, HDR (v2, M12)
  ▼
Display: 30 fps bar (v1), open-world tier at 30 fps (v2), 60/120/240 Hz (beyond 2.0)
```

| Layer | v1 (1.0) | v2 (2.0) | Beyond 2.0 / out |
|---|---|---|---|
| Relinker | ELF → PE, `--to-intel` | ELF output (Linux) | Profile-guided code layout (v3) |
| Runtime / HLE | PRX libraries, futex sync, save data, audio | Linux backend | — |
| Memory manager | Registry (physical allocation → N views), write tracking, aliasing (M5), residency interface | Budgeted residency under VRAM pressure | — |
| GPU translator | GPU IR, indirect on GPU, bindless heap, capture redesign | Async compute, multithreaded recording, RT, mesh | — |
| Shader recompiler | Native optimizer passes, Wave64, specialization | RT and mesh ops | Shader-variant autotuning per GPU (v3) |
| Pipeline cache | Disk cache, async compile with pipeline libraries | — | Ahead-of-time compilation (v3) |
| Profiler | Telemetry, Tracy, perf scenes, regress compare | Memory and RT metrics | Optimization datasets for v3 research |
| Presentation | Frame pacing, composition hook | Upscalers, frame generation, HDR, flip-rate override (experimental) | 60/120/240 Hz targets (v3) |
| Adaptive optimizer (AI) | — | — | v3 research only: proposals verified deterministically, output a local cache (PRD §11) |
| CPU translator | out (guest code runs natively) | out | out |

Everything derived from a title (caches, profiles, autotuning results, patch files) is produced and kept on the user's machine (PRD §11 local-only principle).

## Global policy: no title-specific code

- Core subsystems contain **no title-specific code paths**. AnyPS5 `main` (merged PR #5) has examples that PortPS5 will not adopt:
  - hash- or dword-matched kernel replacements;
  - fixed bindless scan caps;
  - a memo that silently skips a failing dispatch.
- A per-title workaround is allowed only as a documented key in that title's per-game TOML ([configuration.md](configuration.md)). The key names the mechanism it toggles, not the game.
- The `APS5_*` environment switches that merged PR #5 adds (310 unique strings on `main@75a8668`, 121 of them `APS5_NO_*`; 307 at commit `29b4601`) are removed. Tracing, dumps and profiling survive only as a typed `[debug]` config.
- The hosted CI `policy` job enforces this ([verification.md](verification.md)).

## Subsystem specs

| Spec | Subsystem | Decision summary |
|---|---|---|
| [relinker.md](relinker.md) | ELF→PE relinker, NIDs, `--to-intel` | Keep. Adopt the SSE4a lowering from AnyPS5 `main` (merged PR #5). Replace linear-sweep scanning. |
| [guest-memory.md](guest-memory.md) | Guest address space, heap, write tracking | Adopt GuestArena/GuestHeap behind an `IWriteTracker` interface. |
| [libc.md](libc.md) | libc replacement: mspace, heap front-ends, strings, formatting, lifecycle | Port AnyPS5's general libc additions; codes instead of throws, guest errno, System V callbacks. |
| [threading.md](threading.md) | pthread, umtx, sync, time | Replace with futex words on `WaitOnAddress`. |
| [gpu-driver.md](gpu-driver.md) | AGC / PM4 / Vulkan | Adopt Recorder, host import and GPU detile. Redesign capture. Split into modules. |
| [shader-recompiler.md](shader-recompiler.md) | RDNA2 → SSA IR → SPIR-V | Keep. Add a structurizer fallback, a subgroup-size path and tests. |
| [pipeline-cache.md](pipeline-cache.md) | Persistent shader and pipeline cache | Add (1.0 scope). |
| [audio.md](audio.md) | AudioOut2, AJM/ATRAC9 | Adopt from AnyPS5 `main` (merged PR #5). Extend codecs as needed. |
| [video-fmv.md](video-fmv.md) | In-game video, AvPlayer | Keep running the title's own decoders. Use general write tracking. |
| [input.md](input.md) | Pad, keyboard, mouse | Keep SDL. Add mapping and hot-plug. |
| [save-data.md](save-data.md) | Save data and dialogs | Adopt from AnyPS5 `main` (merged PR #5). Replace silent dialog stubs. |
| [image-codecs.md](image-codecs.md) | JPEG/PNG codecs, `libSceJpegEnc`, `libScePngDec` | Shared stb-backed host codec layer under `core/Decoder`; no throws; unsupported modes abort. |
| [host-platform.md](host-platform.md) | Host OS services: virtual memory, futex, threads, faults, files | Add `core/host/` with a Win32 backend in 1.0 (new code only); Linux backend in 2.0. |
| [configuration.md](configuration.md) | Per-game TOML, env-switch migration | Replace env switches with typed TOML. |
| [build-toolchain.md](build-toolchain.md) | CMake, MinGW GCC, CI build, conventions | Keep a pinned GCC 15.2. Move to C++23. GoogleTest dependency pin. |
| [verification.md](verification.md) | CI, test framework, local regression, full runs, results JSON | GoogleTest/GMock adoption, death/perturbation testing, hosted CI without a GPU, plus local results. |

Every subsystem spec uses the same sections: Scope, Current state, Decision, Target design, Interfaces, Failure modes, Tests, Milestones, Open questions.

## Licensing note

The project is GPL-2.0-only. Statically linking SPIRV-Tools (Apache-2.0) into the recompiler, enabled by `ANYPS5_ENABLE_SPIRV_TOOLS` (default off), is a documented open risk ([PRD](../PRD.md) R1). glslang is currently linked into the recompiler library through an unused test file; Milestone 0 makes it test-only ([build-toolchain.md](build-toolchain.md)). No licensing decision is taken now.
