# PortPS5 — Spec: Build and Toolchain

Status: draft v1 · 2026-09-27 · synced with `main` 2026-09-30

## Scope

This spec covers the root and `core/` CMake graph, compiler and linker flags, the pinned toolchain, git submodules, the prx build pipeline (shared library, then objcopy `.eh_frame` rename, then `nid_patcher`), test registration in `ctest`, CMakePresets and CI build wiring, and `docs/CONVENTIONS.md` and `docs/TechnicalDebt.md`. The CI job contents themselves are in [verification.md](verification.md).

## Current state

References are to AnyPS5 `main@e06dbff` (the pre-merge baseline) unless marked `main@75a8668` (current AnyPS5 main, includes merged PR #5).

**Root `CMakeLists.txt`:**
- `cmake_minimum_required(3.20)`, `project(Relinker CXX)` (`:1-2`); `BUILD_TESTING` and `ANYPS5_ENABLE_SPIRV_TOOLS` default OFF (`:4-9`); C++20 required (`:11-12`).
- `-static -static-libgcc -static-libstdc++` appended to the exe linker flags (`:14`).
- SDL2 options are forced (`:16-30`), including `SDL_JOYSTICK OFF`, `SDL_HIDAPI OFF` and `SDL_X11 ON`. SDL2 is configured with the exe linker flags cleared, then restored (`:31-34`). `main@75a8668` keeps the same isolation (`:33-36`) and turns `SDL_JOYSTICK` ON (`:28`).
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
  - `_nid_no_patch` names, `SDL_*` names, and `PortPS5::Config` host exports (`_ZN7PortPS56Config*`) are kept verbatim;
  - `_nid_no_patch_cut` names have the suffix cut;
   - `_nid_disambig<N>` is removed before hashing;
   - duplicate exports are an error (`:28-35`).
   - cross-prx host APIs (called by another prx, never by guest code) must keep a verbatim name, today via a `_nid_no_patch` free function: both sides spell the name with the suffix (e.g. `ResolvePath_nid_no_patch` is exported and imported verbatim). `nid_patcher libc` runs without `--preserve-exports` (`core/libs/CMakeLists.txt:60`), so an undecorated C++ symbol is hashed in the patched DLL while dependents import the verbatim mangled name, and the load fails with `GetLastError` 127. Observed 2026-09-29 on the Dreaming Sarah boot path (`libSceVideoOut` importing the mangled `Config::Loader::IsInitialized` from `libc.prx`). (The `_nid_no_patch_cut` form instead exports the name with the suffix cut, for call sites that must stay unsuffixed; new host APIs follow the `_nid_no_patch` neighbour pattern.)
- **Exports.** `APS5_EXPORT` emits a top-level `__asm__ .globl/.set` alias (`prx/libc/include/general/ExportMacros.hpp:4-7`). `APS5_VABI` is `__attribute__((sysv_abi))` on `_WIN32` (`general/VabiMacros.hpp:4-8`). `specifics/gcc/SymbolAlias.hpp:4-12` defines its aliases only for `__linux__` and expands to nothing on Windows. The Windows ABI-boundary asm is therefore in `ExportMacros.hpp`, not at `SymbolAlias.hpp:7` as the decision table in [README.md](README.md#subsystem-specs) cites.
- **Recompiler** (`core/shader/recompiler/CMakeLists.txt`):
  - it is a CMake function `add_shader_recompiler(target runtime)` instantiated once, from libSceAgcDriver (`prx/libSceAgcDriver/CMakeLists.txt:99`);
  - SPIRV-Tools is built static only if the option is on (`:6-14`, `:164-167`);
  - glslang is always added (`:16-31`) and **linked into the recompiler library** (`:158-161`). The only glslang user is `tests/DummyShaders.cpp`, compiled into the library (`:136`). `RecompileDummy` has no caller outside that file (grep).
- **Submodules:** SDL2, Vulkan-Headers, SPIRV-Headers, SPIRV-Tools, glslang, VulkanMemoryAllocator and LibAtrac9 (`.gitmodules`). VulkanMemoryAllocator has no reference under `core/` on main (grep). LibAtrac9 is referenced from `libSceAjm.native` only.

**Tests.** Registered with `add_test` on `main@e06dbff`:
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

`main@75a8668`, relative to `main@e06dbff`:
- keeps the `core/libs` `BUILD_TESTING` blocks (`core/libs/CMakeLists.txt:124`, `:507`);
- adds `amd64_only_windows` and `amd64_only_converter` relinker tests (`core/relinker/CMakeLists.txt:83-103`, `:122-146`);
- adds an `agc_shader_replay` tool (`prx/libSceAgcDriver/CMakeLists.txt:408-415`), linked to SPIRV-Tools when that is enabled.

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

**Test wiring.** Test suites are standardized on GoogleTest and wired into CTest via a modernized `portps5_add_test(name target LABELS ...)` helper. The helper ensures test executables link `GTest::gtest` and `GTest::gmock`, clears `EXCLUDE_FROM_ALL` under `BUILD_TESTING`, sets runtime DLL lookup paths on Windows (`libc.prx` and MinGW runtime DLLs), and attaches CTest labels:

| Label | Runs in | Members |
|---|---|---|
| `unit` | hosted `unit` job | relinker tests, libc/libkernel `guest_*`, `mspace`, `application_heap`, libc extras (`guest_lifecycle`, `guest_wide_io`, `guest_libc_extras`, `guest_heap_frontend`, `application_heap_default`), `windows_exception`, `exception_runtime`, `agc_command`, `agc_driver_pm4`, AnyPS5 main's (merged PR #5) `amd64_only_*`, ported Kyty kernel/sync/event suites |
| `golden` | hosted `recompiler-golden` | `recompiler_golden_tests` (coverage gate + wave32/64 replay) and `agc_shader_replay --golden` over `core/shader/recompiler/tests/golden/corpus/` (M1) |
| `lavapipe` | hosted `driver-lavapipe` | driver tests that need a Vulkan device (M1) |
| `stress` | local / nightly CI | multithreaded futex/umtx concurrency perturbation tests |
| `local` | maintainer machine only | anything needing a hardware GPU or game data |

Tests are progressively consolidated from standalone single-function executables into cohesive GoogleTest suite binaries discovered via `gtest_discover_tests()`. Python tests become required, so a missing interpreter is a configure error under `BUILD_TESTING`.

**Runtime DLLs.** A POST_BUILD step on the `libs` target copies the three MinGW runtime DLLs from the toolchain `bin/` into the patched `libs/` directory. This matches the relinker default run path `$ORIGIN/libs`. Static linking of the prx runtime stays off; TechnicalDebt records that it conflicts.

**Recompiler dependencies.** `tests/DummyShaders.cpp` moves out of the shipped static library into the test target that needs it, so glslang links only into tests and the build-time tools. The `libSceAgcDriver.prx` import and symbol table is then checked with `objdump -p`/`nm` in CI (the `policy` job) to prove that glslang is absent from release artifacts. The same check reports whether SPIRV-Tools is present, so the R1 status can be recorded; it does not fail on it.

**Dependency pins.** Every dependency the specs add is pinned here, by submodule commit or by a vendored release with its version and SHA-256 recorded next to it. Exact versions are chosen when each lands:

| Dependency | Licence | Used by | Form | Lands |
|---|---|---|---|---|
| GoogleTest (GTest + GMock) | BSD-3-Clause | [verification.md](verification.md) (unit/integration test suites, death testing, mocking) | CMake FetchContent / pinned submodule | M0 / M1 |
| toml++ | MIT, header-only | [configuration.md](configuration.md) | vendored single header `3rdparty/tomlplusplus/toml.hpp` at v3.4.0 (commit `30172438cee64926dc41fdd9c11fb3ba5b2ba9de`, SHA-256 `6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e` of the committed LF bytes, see `3rdparty/tomlplusplus/VERSION.txt`) | M1 |
| stb (`stb_image`, `stb_image_write`) | MIT or public domain | [image-codecs.md](image-codecs.md) | pinned submodule `3rdparty/stb` at `2c980bb5` (PR #58) | M1 (landed) |
| xxHash (XXH3-64/128) | BSD-2 | [shader-recompiler.md](shader-recompiler.md) (hashed keys), [pipeline-cache.md](pipeline-cache.md) (keys and record checksums) | vendored single header at a pinned release | M1 |
| `llvm-mc` (AMDGPU target, `gfx10.3`) | Apache-2.0 with LLVM exception | superseded: the synthetic corpus is hand-assembled dwords with field layouts cited to the decoder sources ([shader-recompiler.md](shader-recompiler.md)), so no `.s` sources and no build-time assembler exist | — | dropped |
| FFmpeg (libavcodec + libavutil, H.264 decoder only) | LGPL-2.1-or-later (plain build, see licence note) | [video-fmv.md](video-fmv.md) (`libSceVideodec2`) | pinned shallow submodule `3rdparty/FFmpeg` at tag `n9.0.2`, commit `946fcce07b6dcd0331c8cc609192aeff5e1924f8`, built by `cmake/PortPS5FFmpeg.cmake` as static libraries | M1 (landed with `libSceVideodec2`) |
| SDL2 | zlib | [input.md](input.md), [audio.md](audio.md) | existing submodule. **Pin check:** confirm that commit `4b69833` has the HIDAPI PS5 driver (*inference:* SDL 2.0.14 or later), or bump the pin | M2 |

**FFmpeg licence note.** PortPS5 is GPL-2.0-only; FFmpeg's default licence is LGPL-2.1-or-later, and a static link into a GPL-2.0 binary is allowed. `--enable-gpl`, `--enable-version3` (LGPLv3 and GPLv3 terms are incompatible with GPL-2.0-only) and `--enable-nonfree` must never be passed, and no external library may be enabled. The exact configure flags are `--enable-static --disable-shared --disable-autodetect --disable-everything --disable-programs --disable-doc --disable-debug --disable-network --disable-avdevice --disable-avformat --disable-avfilter --disable-swresample --disable-swscale --disable-pthreads --disable-w32threads --enable-avcodec --enable-avutil --enable-decoder=h264`, plus `--x86asmexe` only when `nasm` is found (otherwise `--disable-x86asm`). Three checks enforce it: `cmake/PortPS5FFmpegLicenseCheck.cmake` fails the build right after configure if `CONFIG_GPL`, `CONFIG_VERSION3`, `CONFIG_NONFREE`, `CONFIG_GPLV3` or `CONFIG_LGPLV3` is on or `FFMPEG_LICENSE` is not `LGPL version 2.1 or later`; five `ffmpeg_license_gate_*` ctest cases run that script on fixture outputs (a plain LGPL configure passes, GPL/LGPLv3/version3/non-free fail); and `H264DecoderTest.LinkedFfmpegIsPlainLgpl21OrLater` asserts the linked library reports the same string. Source of the pin: https://github.com/FFmpeg/FFmpeg, tag `n9.0.2`. AnyPS5's prebuilt `ffmpeg-core` (KytyPS5's `ext-ffmpeg-core` download) is not used: its configure flags and licence cannot be verified. The `ci` preset sets `PORTPS5_REQUIRE_FFMPEG=ON`, so a runner without `sh`/`make` fails configure instead of silently skipping the decoder tests; other presets fall back to "decoder unavailable" (`CreateDecoder` returns `API_FAIL`).

**SDL options.** `SDL_AUDIO` with the WASAPI backend stays on. From M2, `SDL_JOYSTICK` and `SDL_HIDAPI` are on, as [input.md](input.md) decides; `SDL_HAPTIC` and `SDL_SENSOR` stay off in 1.0.

**Export macro.** A new `APS5_EXPORT_FN` declares the function with `APS5_VABI` and `noexcept`, so no throw crosses the boundary ([threading.md](threading.md) §Error policy), with a `static_assert` proving `sysv_abi` is part of the declared type on MinGW. The NID alias keeps flowing through the existing `APS5_EXPORT` mechanism, so export names stay byte-identical; the macro only guards the declaration. `policy` rejects a raw `APS5_EXPORT(` outside `general/ExportMacros.hpp` once migration is done.

**Conventions.** CONVENTIONS.md keeps the naming rules and Conventional Commits, and replaces the comment rule with "comment why, not what". Each hand-encoded byte sequence, magic constant and ABI-boundary hack needs a one-line reason. TechnicalDebt.md stays human-maintained, gets a `Milestone` tag per item, and loses stale items at M0.

## Interfaces

- [relinker.md](relinker.md): prx export names are NID strings produced by `nid_patcher`; `.ehfram` and `.ehmeta` section names are ABI; `$ORIGIN/libs` is the layout.
- [shader-recompiler.md](shader-recompiler.md) and [pipeline-cache.md](pipeline-cache.md): `add_shader_recompiler` stays a function. The recompiler version constant used as a cache key is generated at configure time from `git describe` plus a manual schema number.
- [verification.md](verification.md): the `ci` preset and the ctest labels are the job entry points; local regression uses `dev`. Toolchain checksum verification happens in CI.
- [audio.md](audio.md): LibAtrac9 submodule. [input.md](input.md): SDL2 `SDL_JOYSTICK` and `SDL_HIDAPI` are enabled in M2, and the SDL pin is checked then.
- [shader-recompiler.md](shader-recompiler.md) and [pipeline-cache.md](pipeline-cache.md): the xxHash pin. The `llvm-mc` build-time tool was dropped: the corpus is hand-assembled dwords. [configuration.md](configuration.md): the toml++ pin.
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
- **`recompiler-golden` job:** builds `recompiler_golden_tests` and `agc_shader_replay` (ci preset), then runs `ctest --preset golden` plus `agc_shader_replay --golden core/shader/recompiler/tests/golden/corpus`. The golden suite carries its own coverage gate, so no count file is needed.
- **`policy` job:** the artifact dependency check above, plus the patterns in [verification.md](verification.md) §1.
- **`doxygen-doc-gate` job:** runs `doxygen docs/Doxyfile` on `core/libs/prx` and `core/relinker` via `.github/workflows/doxygen.yml` on every PR and push to `main` as a required status check. Does not require the MinGW toolchain. Installs the official Doxygen 1.18.0 Windows x64 zip from doxygen.nl, verified against a SHA-256 pinned in the workflow env (cached by version+checksum, retried on download failure). Fails on any malformed Doxygen markup with `WARN_AS_ERROR = FAIL_ON_WARNINGS` so all warnings are logged before failing (`WARN_IF_UNDOCUMENTED` and `WARN_NO_PARAMDOC` are disabled initially to avoid blocking on inherited pre-existing debt). Uploads `build/doxygen_warnings.log` as the `doxygen-warnings` artifact on failure. Complements the Python `check_comments.py` linter, which enforces PortPS5-specific per-file rules (file-level headers, `APS5_VABI` doc coverage, `TEST()` invariant comments) in the main `ci.yml` policy step.
- **Local:** `ctest -L local` before each regression run ([verification.md](verification.md) §2).

## Milestones

- [x] **M0:** C++23; CMakePresets; the pinned toolchain file and CI download with checksum; every existing test in `ctest` with labels; runtime DLL copy; CONVENTIONS rewrite; TechnicalDebt clean-up; `build`, `unit` and `policy` jobs. `DummyShaders` extraction also lands in M0 because it touches licence posture.
- [ ] **M1:** items:
  - [x] the AnyPS5 main (merged PR #5) relinker tests (PR #12, PR #56);
  - [x] the `APS5_EXPORT_FN` migration (PR #11);
  - [x] the toml++ pin (`3rdparty/tomlplusplus`);
  - [ ] `lavapipe` label and job (`driver-lavapipe`): the label exists on the driver suites (`libSceAgcDriver/CMakeLists.txt:351-389`), the job does not (bean `portps5-ekx3`);
  - [ ] the xxHash pin: no xxHash is vendored under `3rdparty/` yet.
- [x] **M1:** `golden` label and job (`recompiler-golden`), the `agc_shader_replay` port from AnyPS5 main (merged PR #5) (adapted: no env switches, return codes, `--golden`/`--dump-corpus` modes).
- [ ] **M2:** `tools/regress` build target and its `local` label (bean `portps5-3m3u`). SDL pin check, with `SDL_JOYSTICK` and `SDL_HIDAPI` enabled (the options are already ON in `CMakeLists.txt:46-48` since PR #20; the pin check against the HIDAPI PS5 driver is still open, bean `portps5-de24`).
- [ ] **M5:** llvm-mingw clang spike (`-gcodeview`, lld PDBs), adopted only if the DWARF unwinder validates.
- [ ] **M6:** release preset used for the release commit, with the R1 status recorded.

## Open questions

1. Does `-fno-asynchronous-unwind-tables` (`core/libs/CMakeLists.txt:20`) interact with the `.ehfram` unwinder on purpose, and does the llvm-mingw spike need `-fdwarf-exceptions` to emit an equivalent `.eh_frame`? The reason for the flag is not recorded.
2. Drop the unused VulkanMemoryAllocator submodule, or keep it for the M3 driver split?
3. Is `-Werror` feasible on prx code in M0, or should it be limited to relinker, nid and tests at first?
4. Does `SDL_X11 ON` on a Windows-only 1.0 cost build time for no benefit?
