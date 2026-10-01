---
name: build-and-test
description: Configure, build and test PortPS5 with the pinned MinGW-w64 GCC 15.2 toolchain and ctest. Use when building, running tests, or diagnosing a build failure.
---

# Build and test

The toolchain facts are in `docs/spec/build-toolchain.md`. Read it first if anything below disagrees with the repo.

1. **Toolchain:**
   - Windows needs MinGW-w64 GCC 15.2 (winlibs, ucrt-posix-seh) on `PATH`, plus CMake 3.25 or later (`CMakePresets.json` uses presets schema v6) and Ninja.
   - Run `g++ --version` and check that it prints 15.2. Don't try MSVC or clang-cl.
2. **Submodules:** `git submodule update --init --recursive`.
3. **Configure and build:** `cmake --preset ci && cmake --build --preset ci` (the CI configuration: warnings as errors, FFmpeg required). `cmake --list-presets` lists the others (`dev`, `release`, `asan`). SPIRV-Tools (`ANYPS5_ENABLE_SPIRV_TOOLS`) is on in `dev`/`ci` and off in `release`, because it is a licence risk in release binaries (PRD R1).
4. **Test:** Run tests in parallel with `ctest --preset ci` (preset specifies bounded parallelism of 4 jobs across supported CTest versions, matching CI; or `ctest --preset ci -j4`). All tests are process-isolated by `portps5_add_gtest` / `gtest_discover_tests`. Running in parallel prevents long-running fuzz tests (such as `GuestArenaExtent.MatchesLinearScanFuzz`) from bottlenecking the suite sequentially. To run a single test, add `-R <name>`.
5. **Report** the exact failing command and the first error, not a summary.

## Rules

- Never "fix" a build by disabling a test, a warning-as-error or a submodule without saying so.
- Tests that need a GPU or game data don't belong in ctest's default set; see `.agents/rules/testing.md`.
