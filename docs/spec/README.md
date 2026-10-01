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
