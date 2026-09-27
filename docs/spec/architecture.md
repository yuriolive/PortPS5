# PortPS5 — Technical Specification: Architecture

Status: draft v1 · 2026-09-27

This spec starts from two trees:

- AnyPS5 `main` at `e06dbff` (about 83k lines of C/C++);
- AnyPS5 PR #5 at `29b4601`, about 113k lines. The PR is 103 commits behind `main`, and 49 files are changed on both sides.

Each subsystem section below gives the current state, then a **Decision** (keep / adopt from PR #5 / replace), the rationale, and the target design. File references are relative to the AnyPS5 tree (`core/...`).

## Execution model (unchanged)

The guest's x86-64 code is not recompiled. The relinker rewrites the decrypted ELF into a Windows PE image. Replacement system libraries (`.prx`, built as shared libraries with NID-patched exports) implement Sony's APIs. Host exports use the System V calling convention through `APS5_VABI` (`__attribute__((sysv_abi))`), so calls between guest and host need no thunks.

## Global policy: no title-specific code

- Core subsystems contain **no title-specific code paths**. PR #5 has examples that PortPS5 will not adopt:
  - a buffer-fill kernel recognised by an exact 9-dword match (`libSceAgcDriver/Driver.cpp:1614-1627`);
  - a memcpy kernel matched by code hash and descriptor words (`Driver.cpp:1841-1860`);
  - fixed bindless scan caps (`ResourceMaterializer.cpp:184,857`);
  - the plan-failure memo that silently skips dispatches.
- A per-title workaround is allowed only as a documented key in that title's per-game TOML (§Configuration). It must name the mechanism it toggles, not the game.
- The `APS5_*` environment-switch surface that PR #5 adds is removed. PR #5 contains 307 unique quoted `APS5_*` strings, of which 121 are `APS5_NO_*` kill switches (counted with `git grep -ohE '"APS5_[A-Z0-9_]+"'`). `main` has none. Kill switches are deleted once their verify mode has passed. Tracing, dumps and profiling survive as a small typed debug config (`[debug]` in TOML, or one `PORTPS5_DEBUG` variable), never as behaviour-changing toggles.

## Relinker

- **Current:** `core/relinker`, about 8.9k lines.
  - It converts ELF to PE with a hand-emitted entry and loader stub (`WindowsEntryStubBuilder.cpp:34-60`).
  - NIDs are resolved through the `nid_patcher` post-build step.
  - Instructions are found by a linear sweep (`InstructionScanner.cpp:27-37`).
  - `main` has a `--to-intel` option (`Amd64OnlyConverter.cpp:41-63`) that has no SSE4a handling. Its substitution table maps AMD-only opcodes to replacements, and when a replacement is longer it shifts following offsets and then truncates (`Amd64OnlyConverter.cpp:55-63`).
  - PR #5 (commit `01c4e3e`) lowers each SSE4a site to a same-length `jmp` into an appended stub section, with branch-target and size checks.
- **Decision:** keep the relinker. Adopt PR #5's `--to-intel`. Replace linear-sweep scanning.
- **Rationale:** the design is sound, and PR #5's same-length lowering is correct where `main`'s shift-and-truncate approach corrupts code. Linear sweep desyncs on data mixed into code, and a CFG already exists in `UnusedNidFilter/ControlFlowGraph.cpp`.
- **Target:**
  - Seed scanning from `.eh_frame` and the existing CFG.
  - Register forms of `EXTRQ`/`INSERTQ` that are too short to lower fall back to the runtime SIGILL trap.
  - An export macro makes it a compile error to forget `APS5_VABI`.

## Guest memory

- **Current:**
  - `main` tracks writes with a vectored exception handler plus `VirtualProtect` fault tracking (`MemoryTrackingWindows.cpp:16-76`). This costs one fault and one syscall per page per re-arm.
  - PR #5 replaces that with `GuestArena`, one reserved arena from `0x10_0000_0000` with `MEM_WRITE_WATCH` (`libc/src/GuestArena.cpp:18-21,89-106`), plus a size-classed `GuestHeap`. The reason: the guest indexes tables by absolute address and breaks when handed host addresses above 1 TiB.
- **Decision:** adopt `GuestArena` and `GuestHeap`, and put tracking behind an interface.
- **Rationale:** `GetWriteWatch` was measured far cheaper than fault tracking. An interface keeps a later Linux backend possible.
- **Target:**
  - An `IWriteTracker` interface with a Windows write-watch implementation.
  - The arena's O(n) first-fit scan becomes a free-list or buddy allocator.
  - Tracking granularity starts at 64 KiB blocks, with sub-block refinement where false invalidation shows up in profiles.

## Threading and synchronization

- **Current (both trees):** guest `pthread` mutexes and condition variables wrap heap-allocated `std::timed_mutex`, `recursive_timed_mutex` and `condition_variable_any` on winpthreads (`libkernel/.../Pthread.hpp:25-39`).
  - Every lock, unlock, trylock and signal also takes a process-global mutex (`Mutex.cpp:21,138,142,161`; `Cond.cpp:20,129-140`), so all guest locking is serialized.
  - `_umtx_op` is not implemented.
  - Real POSIX errors throw: for example, an error-checking mutex relock throws instead of returning `EDEADLK` (`Mutex.cpp:40`).
- **Decision:** replace.
- **Rationale:** this is the largest CPU-side throughput bottleneck, and it can deadlock under engine job systems.
- **Target:**
  - Futex words live in place in the guest object: 4 or 8 bytes, lazily initialised with CAS. They use `WaitOnAddress`, `WakeByAddressSingle` and `WakeByAddressAll`. Timed and recursive variants are layered on top.
  - `_umtx_op`, event flags and semaphores share the same primitive.
  - Real POSIX and SCE errors return codes. Truly unsupported states go through a `[[noreturn]] Unsupported()` that logs and aborts, replacing `throw std::runtime_error`, which the shared unwinder lets guest `catch(...)` swallow.

## GPU driver (AGC / PM4 / Vulkan)

- **Current:**
  - `main` submits and fence-waits per dispatch, and creates and destroys the module, pipeline and fence on every call (`VulkanDevice.cpp:759-800`). That is not viable.
  - PR #5 adds:
    - a `Recorder`: one open command buffer per device, a timeline semaphore, in-order reap (`Graphics/Recorder.cpp`, 2,901 lines);
    - `VK_EXT_external_memory_host` zero-copy import of guest memory (`GuestBufferMemory.cpp`);
    - GPU detile from the addrlib GFX10 swizzle equations (`Graphics/shaders/TextureDetile.comp`);
    - dispatch and draw recipe caches.
  - Gaps:
    - depth/stencil is rejected (`State.cpp:320,520`);
    - `DRAW_INDIRECT` is not implemented;
    - `Driver.cpp` is 6,214 lines that mix concerns.
- **Decision:** adopt PR #5's Recorder, host import and GPU detile. Replace CPU-side capture. Split the driver into modules.
- **Rationale:** the Recorder is the standard emulator shape, and the PR #5 description reports it took a frame from about 13 s to about 0.1 s. Capture, however, reads guest memory on the CPU at record time, while hardware reads it at execute time. Batching widens that window, which causes the known wedge ("guest memory is not readable at 0x60"). Point fixes will keep recurring.
- **Target:**
  - Modules: `CommandProcessor` (PM4 per queue), `Recorder`, `BufferCache`, `TextureCache`, `PipelineCache`, `Rasterizer` state, `Presenter`.
  - Buffer resources are resolved on the GPU through device addresses, as the shader-side BDA path already does.
  - Image descriptors are resolved at submit time, behind the ordering fence. A wait is never satisfied from an unexecuted label while a capture on that queue depends on it.
  - Add depth/stencil, the indirect draw family, and conditional colour writes.
  - Host-import budget: sized automatically from available memory, with a staging-buffer fallback when the import budget is exhausted.
  - Bindless: a GPU-side descriptor heap using `VK_EXT_descriptor_indexing` or `VK_EXT_descriptor_buffer`, instead of CPU material scans.
  - Fill and copy kernels are recognised by general IR patterns (uniform store, linear copy), not by hashes.

## Shader recompiler

- **Current:** `core/shader/recompiler`, nearly identical in both trees.
  - It decodes RDNA2, then builds a Braun-style SSA IR with phi nodes and block sealing (`IrBlock.hpp:32-45`, `Optimization/src/SsaBuilder`).
  - A structurizer handles dominators and loop canonicalisation, but it throws on irreducible control flow (`ControlFlow/src/Structurizer.cpp:875`).
  - Its own SPIR-V emitter output is validated and optimised with SPIRV-Tools (`SpirvOptimizer.cpp:34-44`).
  - Wave64 on a 32-wide host is emulated as two lanes per invocation (`SpirvEmitter.cpp:217`), and `VK_EXT_subgroup_size_control` is never used.
  - Compilation happens at runtime, specialised on descriptor contents (`Recompiler.cpp:289-300`). The variant cache is in-memory only and searched by linear scan.
  - There are no unit tests.
- **Decision:** keep. Replace the structurizer's failure path. Add a subgroup-size path and tests.
- **Rationale:** the pipeline shape matches shadPS4 and yuzu, and validation on every module is valuable. The gaps are local.
- **Target:**
  - Fall back to goto-elimination structurizing, so it always succeeds.
  - Force 64-wide subgroups where the device supports it, and keep the two-lane emulation otherwise.
  - Hash-indexed variant lookup with a bound.
  - Golden SPIR-V tests replayed from serialised requests (`RequestSerializer.cpp`, the `agc_shader_replay` tool).
  - Relink-time AOT compilation stays post-1.0.

## Audio

- **Current (PR #5):**
  - AudioOut2 runs on one SDL F32 stereo 48 kHz device per context, with port mixing and 7.1 folded to stereo. The queue level comes from SDL's queued bytes, which fixed 4–5× over-speed.
  - `libSceAjm.native` decodes ATRAC9 through the LibAtrac9 submodule.
  - Object-port positions are ignored, and other AJM codecs fail once with an error.
- **Decision:** adopt.
- **Rationale:** the PR #5 description reports correct pacing (104 s of audio in 103 s of wall time).
- **Target:**
  - Add codecs as gate titles require them, with an inventory in Milestone 1.
  - Implement object-port panning.
  - WASAPI shared mode behind SDL, with no exclusive mode for 1.0.

## Video / FMV

- **Current:** PR #5 plays Bink 2 through the title's own compute-shader decoder. It relies on the `s_*_saveexec` operand fix and on "adjacent-generation" write-back for video planes. `libSceAvPlayer` exists but its coverage is unknown.
- **Decision:** keep the path of running the title's own decoder. Replace the Bink-plane special case with general block-generation tracking.
- **Rationale:** games decode their own Bink, so correct generic GPU memory tracking is the real requirement.
- **Target:** AvPlayer coverage audit in Milestone 1. Any system-decoded formats (H.264/H.265) use Media Foundation. FMV playback is a gate requirement, and skipping a video is not a pass.

## Input

- **Current:** `libScePad` implements `scePadRead` over SDL. TechnicalDebt.md lists keyboard/mouse remapping as missing.
- **Decision:** keep SDL, and extend it.
- **Rationale:** SDL covers XInput and DualSense over USB.
- **Target:**
  - A keyboard/mouse mapping and a bindings table in TOML.
  - Hot-plug support and controller-number assignment.
  - DualSense haptics and adaptive triggers are post-1.0.

## Save data

- **Current (PR #5):** `libSceSaveData.native` handles mounts, backup events and memory slots persisted per slot. Short mount points go through the libc path-alias table. `libSceSaveDataDialog.native` and `libSceCommonDialog` are silent stubs.
- **Decision:** adopt, and replace the silent dialog stubs.
- **Rationale:** save/load is a gate requirement, and a silently stubbed dialog can hide a failure to save.
- **Target:**
  - Saves live under `%LOCALAPPDATA%/PortPS5/saves/<titleId>/`.
  - Dialog stubs return scripted and logged results.
  - A save round-trip test per gate title is part of local regression.

## Configuration

- **Current:** PR #5 adds about 300 `APS5_*` environment variables, each read once through `getenv`. `main` has none.
- **Decision:** replace with per-game TOML.
- **Rationale:** the switch space can't be tested, and per-title behaviour needs a reviewable home.
- **Target:**
  - `config/global.toml`, plus `config/games/<titleId>.toml` with the sections `[display]`, `[input]`, `[workarounds]` and `[debug]`.
  - A typed schema is validated at start-up, and unknown keys are an error.
  - Every `[workarounds]` key is documented in `docs/workarounds.md` with the mechanism it toggles and the titles that use it.

## Pipeline cache

- **Current:** there is no persistence. Compiled SPIR-V and pipelines are lost when the process exits.
- **Decision:** add one (in 1.0 scope).
- **Rationale:** it is required for a warm-cache performance bar, and it is cheap because serialised requests already exist.
- **Target:**
  - An on-disk cache per title, keyed by recompiler version, request hash and driver UUID. It holds SPIR-V, a `VkPipelineCache` blob and variant metadata.
  - A cache is invalidated when the recompiler version or the device changes.

## Build and toolchain

- **Current:**
  - CMake ≥3.20, C++20, and MinGW-w64 GCC 15.2 (winlibs, ucrt-posix-seh) only, built with `-static` runtime flags. There is no CMakePresets file.
  - GCC-specifics sit on the ABI boundary: `sysv_abi`, top-level `__asm__` symbol aliases (`specifics/gcc/SymbolAlias.hpp:7`), and a custom DWARF unwinder that relies on `.eh_frame` renamed with objcopy.
  - Submodules: SDL2, Vulkan-Headers, SPIRV-Headers, SPIRV-Tools (optional), glslang, VulkanMemoryAllocator, LibAtrac9.
  - `docs/CONVENTIONS.md` forbids code comments.
- **Decision:**
  - Keep GCC 15.2 as the baseline, with a pinned version.
  - Bump to C++23.
  - Replace the no-comments rule with "comment why, not what".
  - Evaluate llvm-mingw clang as an optional investigation.
- **Rationale:**
  - MSVC can't express `sysv_abi`, so it is off the table.
  - C++23 is only a flag change.
  - Unexplained magic bytes block contributors (for example `WindowsEntryStubBuilder.cpp:60`).
- **Target:** CMakePresets, pinned toolchain download in CI, and `ctest` wiring for every test target. `clang -gcodeview` with lld PDBs is tried as a spike in Milestone 5 and adopted only if the DWARF unwinder validates.

## Licensing note

The project is GPL-2.0-only. Statically linking SPIRV-Tools (Apache-2.0) into the recompiler, enabled by `ANYPS5_ENABLE_SPIRV_TOOLS` (default off), is a documented open risk (PRD R1). glslang is used only at build time. No decision is taken now.
