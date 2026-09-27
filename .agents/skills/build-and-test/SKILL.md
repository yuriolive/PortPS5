---
name: build-and-test
description: Configure, build and test PortPS5 with the pinned MinGW-w64 GCC 15.2 toolchain and ctest. Use when building, running tests, or diagnosing a build failure.
---

# Build and test

The toolchain facts are in `docs/spec/build-toolchain.md`. Read it first if anything below disagrees with the repo.

1. **Toolchain:**
   - Windows needs MinGW-w64 GCC 15.2 (winlibs, ucrt-posix-seh) on `PATH`, plus CMake 3.20 or later and Ninja.
   - Run `g++ --version` and check that it prints 15.2. Don't try MSVC or clang-cl.
2. **Submodules:** `git submodule update --init --recursive`.
3. **Configure:**
   - Use a preset when `CMakePresets.json` exists (`cmake --list-presets`).
   - Otherwise run `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON`.
   - Add `-DANYPS5_ENABLE_SPIRV_TOOLS=ON` only for dev and validation builds. It is a licence risk in release binaries (PRD R1).
4. **Build:** `cmake --build build`.
5. **Test:** `ctest --test-dir build --output-on-failure`. To run one test, add `-R <name>`.
6. **Report** the exact failing command and the first error, not a summary.

## Rules

- Never "fix" a build by disabling a test, a warning-as-error or a submodule without saying so.
- Tests that need a GPU or game data don't belong in ctest's default set; see `.agents/rules/testing.md`.
