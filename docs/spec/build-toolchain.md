# PortPS5 — Spec: Build and Toolchain

Status: draft v1 · 2026-09-27

## Scope

This spec covers the root and `core/` CMake graph, compiler and linker flags, the pinned toolchain, git submodules, the prx build pipeline (shared library, then objcopy `.eh_frame` rename, then `nid_patcher`), test registration in `ctest`, CMakePresets and CI build wiring, and `docs/CONVENTIONS.md` and `docs/TechnicalDebt.md`. The CI job contents themselves are in [verification.md](verification.md).

## Current state

References are to AnyPS5 `main` (`e06dbff`) unless marked PR #5 (`29b4601`).

**Root `CMakeLists.txt`:**
- `cmake_minimum_required(3.20)`, `project(Relinker CXX)` (`:1-2`); `BUILD_TESTING` and `ANYPS5_ENABLE_SPIRV_TOOLS` default OFF (`:4-9`); C++20 required (`:11-12`).
- `-static -static-libgcc -static-libstdc++` appended to the exe linker flags (`:14`).
- SDL2 options are forced (`:16-30`), including `SDL_JOYSTICK OFF`, `SDL_HIDAPI OFF` and `SDL_X11 ON`. SDL2 is configured with the exe linker flags cleared, then restored (`:31-34`); PR #5 lacks this isolation.
- Subdirectories are added in order: recompiler, libs, relinker (`:36-38`).

**`core/libs/CMakeLists.txt` (prx pipeline):**

| Stage | Location | Behaviour |
|---|---|---|
| NID tool | `:6-17` | `nid_patcher` host executable, `cxx_std_20`. |
| Unwind flags | `:19-21` | `-fno-asynchronous-unwind-tables` for every MinGW target added after this point. |
| `.eh_frame` rename | `:23-33` | `configure_windows_unwind()` runs a POST_BUILD step, `objcopy --rename-section .eh_frame=.ehfram`. It is a FATAL_ERROR if objcopy is missing. |
| libc | `:35-61` | `SHARED EXCLUDE_FROM_ALL`. Unpatched output goes to `libs/unpatched/libc.prx`, then is copied and patched with `nid_patcher libc`. |
| Other prx | `:63-107` | Every directory under `prx/` is globbed. Each links `libkernel` and `libc` (`:81-85`) and is patched with `--preserve-exports <unpatched libc>` (`:97-103`). |
| Aggregate | `:109` | `add_custom_target(libs ...)` is **not ALL**. Because every prx is `EXCLUDE_FROM_ALL`, a plain `cmake --build` builds no patched prx. |

- **Unwinder contract.** libc's DWARF unwinder locates `.ehmeta` and `.ehfram` by PE section name (`prx/libc/src/exception/Unwind.cpp:158,179`). PE image section names are 8 bytes, so a 9-byte `.eh_frame` cannot be matched this way. This is an inference about the reason for the rename.
- **NID naming** (`nid/include/nid/NidPatcherUtils.hpp:15-32`, `nid/src/NidResolver.cpp:23-66`):
  - a `_nid_postfix` suffix is stripped and the NID is computed;
  - `_nid_no_patch` names and `SDL_*` names are kept verbatim;
  - `_nid_no_patch_cut` names have the suffix cut;
  - `_nid_disambig<N>` is removed before hashing;
  - duplicate exports are an error (`:28-35`).
- **Exports.** `APS5_EXPORT` emits a top-level `__asm__ .globl/.set` alias (`prx/libc/include/general/ExportMacros.hpp:4-7`). `APS5_VABI` is `__attribute__((sysv_abi))` on `_WIN32` (`general/VabiMacros.hpp:4-8`). `specifics/gcc/SymbolAlias.hpp:4-12` defines its aliases only for `__linux__` and expands to nothing on Windows. The Windows ABI-boundary asm is therefore in `ExportMacros.hpp`, not at `SymbolAlias.hpp:7` as the decision table in [README.md](README.md#subsystem-specs) cites.
- **Recompiler** (`core/shader/recompiler/CMakeLists.txt`):
  - it is a CMake function `add_shader_recompiler(target runtime)` instantiated once, from libSceAgcDriver (`prx/libSceAgcDriver/CMakeLists.txt:99`);
  - SPIRV-Tools is built static only if the option is on (`:6-14`, `:164-167`);
  - glslang is always added (`:16-31`) and **linked into the recompiler library** (`:158-161`). The only glslang user is `tests/DummyShaders.cpp`, compiled into the library (`:136`). `RecompileDummy` has no caller outside that file (grep).
- **Submodules:** SDL2, Vulkan-Headers, SPIRV-Headers, SPIRV-Tools, glslang, VulkanMemoryAllocator and LibAtrac9 (`.gitmodules`). VulkanMemoryAllocator has no reference under `core/` on main (grep). LibAtrac9 is referenced from `libSceAjm.native` only.

**Tests.** Registered with `add_test` on main:
- libc and libkernel `guest_*` tests and `mspace` (`core/libs/CMakeLists.txt:111-358`);
- `agc_driver`, `agc_shader_memory`, `agc_driver_flip` and `agc_driver_pm4` (`prx/libSceAgcDriver/CMakeLists.txt:260-270`);
- relinker `strict_nid_filter`, plus three Python tests that are skipped silently when Python is absent (`core/relinker/CMakeLists.txt:82-117`);
- `windows_dependency_diagnostics` (`:64-79`).

Built but **not registered**:
- `application_heap_tests` (`core/libs/CMakeLists.txt:360-362`);
- `windows_exception_tests`, which also strips the static flags (`:364-372`);
- `agc_command_tests` (`prx/libSceAgc/CMakeLists.txt:64`);
- `agc_driver_recompiler_tests`, `agc_driver_graphics_tests` and `agc_driver_bda_device_tests` (`prx/libSceAgcDriver/CMakeLists.txt:130,188,241`);
- `video_out_flip_tests` (`prx/libSceVideoOut/CMakeLists.txt:25`);
- `core/libs/tests/run_exception_tests.py`, which has no CMake reference.

PR #5:
- drops main's whole `core/libs` `BUILD_TESTING` block (diff), since it is 103 commits behind;
- adds `amd64_only_converter` and `amd64_only_windows` relinker tests;
- adds an `agc_shader_replay` tool (`prx/libSceAgcDriver/CMakeLists.txt:393-401`), linked to SPIRV-Tools when that is enabled.

**Docs:**
- `docs/CONVENTIONS.md` allows code comments only for human-added technical-debt markers and `#endif`/namespace ends.
- `docs/TechnicalDebt.md` says:
  - the pinned winlibs GCC 15.2.0 `x86_64-ucrt-posix-seh` toolchain is required;
  - prx files need `libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll` next to them;
  - `sce_module` loading is not implemented. That line appears stale, since main now has `GuestModuleBuilder` and `--skip-sce-module`; to verify at M0.

## Decision

These are consistent with the decision table in [README.md](README.md#subsystem-specs) §Build and toolchain:
- Keep MinGW-w64 GCC 15.2 (winlibs, ucrt-posix-seh) as the only supported toolchain, pinned by version and checksum.
- Bump to C++23.
- Add CMakePresets.
- Register every test target in `ctest`.
- Replace the no-comments rule with "comment why, not what".
- llvm-mingw clang is an M5 spike only. MSVC stays out, because it cannot express `sysv_abi`.
- Keep the objcopy rename and `nid_patcher` pipeline unchanged in behaviour.

## Target design

**Toolchain pin.** `cmake/toolchains/mingw-gcc15.cmake` sets the compilers and `CMAKE_OBJCOPY`. At configure time it fails unless `CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL 15.2.0` and the target triple is `x86_64-w64-mingw32`. CI downloads the winlibs archive from a fixed URL and verifies a SHA-256 recorded in `cmake/toolchains/mingw-gcc15.sha256`. The values are filled in M0.

**Presets** (`CMakePresets.json`, schema 6):

| Preset | Purpose | Key cache vars |
|---|---|---|
| `release` | user build | `CMAKE_BUILD_TYPE=Release`, `BUILD_TESTING=OFF`, `ANYPS5_ENABLE_SPIRV_TOOLS=OFF` |
| `dev` | development, local regression | `Debug`, `BUILD_TESTING=ON`, `ANYPS5_ENABLE_SPIRV_TOOLS=ON` |
| `ci` | hosted CI, including `recompiler-golden` | inherits `dev` (so SPIRV-Tools is ON), `CMAKE_COMPILE_WARNING_AS_ERROR=ON` for PortPS5 targets only |

SPIRV-Tools is ON in `dev` and `ci`. In `release` it is OFF by default, pending the PRD R1 decision. That is the current default, not a policy-enforced rule; R1 may change it.

Build presets name the `relinker` and `libs` targets explicitly, so that patched prx are always produced. Test presets filter by label.

**Language level.** Set `CMAKE_CXX_STANDARD 23` once at the root, and delete the per-target `cxx_std_20` features in `core/libs/CMakeLists.txt:17` and `core/shader/recompiler/CMakeLists.txt:151`, the only two sites. Existing `CXX_EXTENSIONS OFF` properties stay. The rest keep GNU extensions, because `APS5_EXPORT` relies on GNU asm.

**Test wiring.** A helper `portps5_add_test(name target LABELS ...)` does three things: it clears `EXCLUDE_FROM_ALL` when `BUILD_TESTING` is on, it applies the libc `PATH` prepend that is now repeated 27 times, and it attaches labels:

| Label | Runs in | Members |
|---|---|---|
| `unit` | hosted `unit` job | relinker tests, libc/libkernel `guest_*`, `mspace`, `application_heap`, `windows_exception`, `exception_runtime`, `agc_command`, `agc_driver_pm4`, PR #5 `amd64_only_*` |
| `golden` | hosted `recompiler-golden` | `agc_shader_replay` over `tests/golden/` (M1) |
| `lavapipe` | hosted `driver-lavapipe` | driver tests that need a Vulkan device (M1) |
| `local` | maintainer machine only | anything needing a hardware GPU or game data |

Tests currently unregistered are classified into these labels at M0 by running each one on a GPU-less runner (inference: `bda_device`, `graphics` and `flip` probably need a device). Python tests become required, so a missing interpreter is a configure error under `BUILD_TESTING`.

**Runtime DLLs.** A POST_BUILD step on the `libs` target copies the three MinGW runtime DLLs from the toolchain `bin/` into the patched `libs/` directory. This matches the relinker default run path `$ORIGIN/libs`. Static linking of the prx runtime stays off; TechnicalDebt records that it conflicts.

**Recompiler dependencies.** `tests/DummyShaders.cpp` moves out of the shipped static library into the test target that needs it, so glslang links only into tests and the build-time tools. The `libSceAgcDriver.prx` import and symbol table is then checked with `objdump -p`/`nm` in CI (the `policy` job) to prove that glslang is absent from release artifacts. The same check reports whether SPIRV-Tools is present, so the R1 status can be recorded; it does not fail on it.

**Dependency pins.** Every dependency the specs add is pinned here, by submodule commit or by a vendored release with its version and SHA-256 recorded next to it. Exact versions are chosen when each lands:

| Dependency | Licence | Used by | Form | Lands |
|---|---|---|---|---|
| toml++ | MIT, header-only | [configuration.md](configuration.md) | vendored single header `3rdparty/tomlplusplus/toml.hpp` at v3.4.0 (commit `30172438cee64926dc41fdd9c11fb3ba5b2ba9de`, SHA-256 `6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e` of the committed LF bytes, see `3rdparty/tomlplusplus/VERSION.txt`) | M1 |
| xxHash (XXH3-64/128) | BSD-2 | [shader-recompiler.md](shader-recompiler.md) (hashed keys), [pipeline-cache.md](pipeline-cache.md) (keys and record checksums) | vendored single header at a pinned release | M1 |
| `llvm-mc` (AMDGPU target, `gfx10.3`) | Apache-2.0 with LLVM exception | [shader-recompiler.md](shader-recompiler.md) synthetic corpus | **build-time tool only**, never linked. It regenerates the checked-in `.req` and `.spvasm` from `.s` sources; the LLVM release is pinned, and CI verifies it before use | M1 |
| SDL2 | zlib | [input.md](input.md), [audio.md](audio.md) | existing submodule. **Pin check:** confirm that commit `4b69833` has the HIDAPI PS5 driver (*inference:* SDL 2.0.14 or later), or bump the pin | M2 |

**SDL options.** `SDL_AUDIO` with the WASAPI backend stays on. From M2, `SDL_JOYSTICK` and `SDL_HIDAPI` are on, as [input.md](input.md) decides; `SDL_HAPTIC` and `SDL_SENSOR` stay off in 1.0.

**Export macro.** A new `APS5_EXPORT_FN` declares the function with `APS5_VABI` and `noexcept`, so no throw crosses the boundary ([threading.md](threading.md) §Error policy), with a `static_assert` proving `sysv_abi` is part of the declared type on MinGW. The NID alias keeps flowing through the existing `APS5_EXPORT` mechanism, so export names stay byte-identical; the macro only guards the declaration. `policy` rejects a raw `APS5_EXPORT(` outside `general/ExportMacros.hpp` once migration is done.

**Conventions.** CONVENTIONS.md keeps the naming rules and Conventional Commits, and replaces the comment rule with "comment why, not what". Each hand-encoded byte sequence, magic constant and ABI-boundary hack needs a one-line reason. TechnicalDebt.md stays human-maintained, gets a `Milestone` tag per item, and loses stale items at M0.

## Interfaces

- [relinker.md](relinker.md): prx export names are NID strings produced by `nid_patcher`; `.ehfram` and `.ehmeta` section names are ABI; `$ORIGIN/libs` is the layout.
- [shader-recompiler.md](shader-recompiler.md) and [pipeline-cache.md](pipeline-cache.md): `add_shader_recompiler` stays a function. The recompiler version constant used as a cache key is generated at configure time from `git describe` plus a manual schema number.
- [verification.md](verification.md): the `ci` preset and the ctest labels are the job entry points; local regression uses `dev`. Toolchain checksum verification happens in CI.
- [audio.md](audio.md): LibAtrac9 submodule. [input.md](input.md): SDL2 `SDL_JOYSTICK` and `SDL_HIDAPI` are enabled in M2, and the SDL pin is checked then.
- [shader-recompiler.md](shader-recompiler.md) and [pipeline-cache.md](pipeline-cache.md): the xxHash pin and the `llvm-mc` build-time tool. [configuration.md](configuration.md): the toml++ pin.
- [configuration.md](configuration.md): no build-time behaviour switches. Debug features are runtime `[debug]` keys, not CMake options.

## Failure modes

| Failure | Handling |
|---|---|
| Wrong compiler version | Configure error naming the expected version. |
| objcopy missing | Existing FATAL_ERROR (`core/libs/CMakeLists.txt:25-27`). |
| `nid_patcher` duplicate export | Build fails (`NidResolver.cpp:28-35`). It is never downgraded to a warning. |
| Plain `cmake --build` without `libs` | Fixed by the build presets. The CI `build` job runs the preset, not raw CMake. |
| Missing runtime DLLs at launch | Prevented by the copy step. The loader prints `Failed to load module` with `GetLastError` (relinker stub). |
| SPIRV-Tools in a release artifact | The `policy` job reports it for the R1 record. Release defaults to OFF pending PRD R1. |
| glslang in a release artifact | The `policy` job fails. |
| A dependency without a recorded pin | Review rejects it; every new dependency goes in the pin table above. |
| Python absent with tests enabled | Configure error (today: silently skipped, `core/relinker/CMakeLists.txt:92`). |

## Tests

- **`build` job:** configure and build the `ci` preset from a clean clone, with submodules at their recorded SHAs.
- **`unit` job:** `ctest --preset ci -L unit`. Its gate is that the total test count must not shrink from one commit to the next; the count is stored in a checked-in `tests/expected-count`.
- **`policy` job:** the artifact dependency check above, plus the patterns in [verification.md](verification.md) §1.
- **Local:** `ctest -L local` before each regression run ([verification.md](verification.md) §2).

## Milestones

- **M0:** C++23; CMakePresets; the pinned toolchain file and CI download with checksum; every existing test in `ctest` with labels; runtime DLL copy; CONVENTIONS rewrite; TechnicalDebt clean-up; `build`, `unit` and `policy` jobs. `DummyShaders` extraction also lands in M0 because it touches licence posture.
- **M1:** `golden` and `lavapipe` labels and jobs (`recompiler-golden`, `driver-lavapipe`), the `agc_shader_replay` port from PR #5, the PR #5 relinker tests, and the `APS5_EXPORT_FN` migration. The toml++ and xxHash pins, and the pinned `llvm-mc` build-time tool for the synthetic corpus.
- **M2:** `tools/regress` build target and its `local` label. SDL pin check, with `SDL_JOYSTICK` and `SDL_HIDAPI` enabled.
- **M5:** llvm-mingw clang spike (`-gcodeview`, lld PDBs), adopted only if the DWARF unwinder validates.
- **M6:** release preset used for the release commit, with the R1 status recorded.

## Open questions

1. Does `-fno-asynchronous-unwind-tables` (`core/libs/CMakeLists.txt:20`) interact with the `.ehfram` unwinder on purpose, and does the llvm-mingw spike need `-fdwarf-exceptions` to emit an equivalent `.eh_frame`? The reason for the flag is not recorded.
2. Drop the unused VulkanMemoryAllocator submodule, or keep it for the M3 driver split?
3. Is `-Werror` feasible on prx code in M0, or should it be limited to relinker, nid and tests at first?
4. Does `SDL_X11 ON` on a Windows-only 1.0 cost build time for no benefit?
