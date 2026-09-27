# PortPS5 — Testing Architecture & Verification Guide

This document defines the testing architecture, standards, and modernization roadmap for PortPS5, incorporating best practices from projects like **shadps4**, **Dolphin**, and **RPCS3**.

---

## 1. Principles & Anti-Breakage Rules

1. **No Fragile Hacks:** Tests must provide clear, actionable diagnostics. Ad-hoc assertions that call bare `std::abort()` without error messages or line numbers are forbidden in new code.
2. **Standardized Framework:** All unit, integration, and subsystem tests use **GoogleTest (GTest)** and **GMock**.
3. **Death Tests for Fatal Paths:** In PortPS5, invalid guest calls or precondition failures invoke the logging abort path (`APS5_ABORT`). GoogleTest death tests (`EXPECT_DEATH`) must verify that invalid calls abort safely with expected diagnostics without killing the test runner.
4. **Legal & Policy Cleanliness:**
   - Tests must never contain real Sony keys, firmware dumps, or game assets.
   - When porting tests from reference projects, all Title IDs must be scrubbed and replaced with generic synthetic mocks (e.g. `PPSA00000`).
   - Title-specific branching is strictly forbidden in `core/` ([.agents/rules/no-title-hacks.md](../.agents/rules/no-title-hacks.md)).
5. **Expression Decomposition:** Always use `EXPECT_EQ`, `ASSERT_NE`, `EXPECT_TRUE`, etc. so that failures clearly print actual vs expected values (e.g. `errno: 22 (EINVAL)`).

---

## 2. Framework & CMake Wiring

### Test Suites Structure
Instead of compiling dozens of individual executables for single functions, tests are organized into cohesive test suites:

- `core/relinker/tests/`: Relinker binary parsing, SysV-to-Win64 ABI lowering, TLS, and relocations.
- `core/libs/tests/kernel/`: `libkernel` synchronization, threads, event queues, and timing.
- `core/libs/tests/memory/`: Virtual memory allocation, 16 KB page alignment, page protections, and write tracking.
- `core/libs/tests/filesystem/`: POSIX filesystem semantics, mount point isolation, and save data.
- `core/shader/recompiler/tests/`: Instruction recompiler verification, CFG reconvergence, and golden SPIR-V checks.
- `core/libs/tests/audio/`: `AudioOut2` and ATRAC9 decoding with GMock audio stream fixtures.
- `core/libs/tests/input/`: `libScePad` DualSense/XInput state machines with mocked SDL streams.

### CMake Registration
Tests are registered using `portps5_add_test` and `gtest_discover_tests()`:
- Automatically links `GTest::gtest`, `GTest::gmock`, and `GTest::gtest_main`.
- Prepends `$<TARGET_FILE_DIR:libc>` to `PATH` on Windows so runtime DLLs are resolved.
- Attaches CTest labels:
  - `unit`: Fast CPU/memory/relinker tests running in hosted CI on every PR.
  - `golden`: Recompiler golden SPIR-V diffs validated via `spirv-val`.
  - `lavapipe`: Driver tests running against Vulkan software rasterizer.
  - `stress`: Multi-threaded futex/event concurrency chaos tests.
  - `local`: Maintainer GPU-only title validation.

---

## 3. Reference Test Porting from Reference Projects

Reference open-source implementations (such as KytyPS5) provide valuable low-level test cases that are systematically adapted into PortPS5 GoogleTest suites:

| Reference Test Source | Target PortPS5 Suite | Key Coverage |
| :--- | :--- | :--- |
| `SyncOnAddressTests.cpp` | `tests/kernel/SyncOnAddressTests.cpp` | `Wait32`, `Wait64`, `Wake`, misalignment rejection, timeouts, concurrent wakeups. |
| `EventQueueLifetimeTests.cpp` | `tests/kernel/EventQueueLifetimeTests.cpp` | Event queues, filters (`EVFILT_READ`, `EVFILT_TIMER`, `EVFILT_USER`), duplicate events, concurrent destruction. |
| `VirtualMemoryAllocationTests.cpp` | `tests/memory/VirtualMemoryAllocationTests.cpp` | 16 KB pages, memory protection flags, memory pools, direct memory mapping, fibers, red zone patching. |
| `MemoryTrackerTests.cpp` | `tests/memory/MemoryTrackerTests.cpp` | Dirty tracking, page protection transitions, range sets. |
| `KernelFileSystemTests.cpp` | `tests/kernel/KernelFileSystemTests.cpp` | Descriptor renames while open, mount point isolation (`/savedata0`), path canonicalization. |
| `SaveDataMemoryTests.cpp` | `tests/kernel/SaveDataMemoryTests.cpp` | User slots, memory setup, crash-safe save mounts, unmount safety. |
| `AudioOut2PortTests.cpp` | `tests/audio/AudioOut2PortTests.cpp` | Audio ports, PCM streaming, volume, rerouting, device open/close state machines. |
| `PadHapticsTests.cpp` | `tests/input/PadHapticsTests.cpp` | DualSense rumble and haptics emulation with mocked SDL streams. |
| `ShaderRecompilerComputeTests.cpp` | `tests/shader/ComputeInstructionsTest.cpp` | Full RDNA2 compute instruction set verification. |
| `shaderCfgTests.cpp` | `tests/shader/ShaderCfgTest.cpp` | CFG reconstruction, SSA rewrite, dead code elimination, constant propagation, SPIR-V validation. |

---

## 4. Advanced Testing Methodologies

### Concurrency Perturbation Testing (Chaos Scheduling)
To expose race conditions, missed wakeups, and deadlocks in synchronization primitives (`WaitOnAddress`, `futex`, `umtx`, `equeues`):
- Test builds instrument atomic wait/wake points with randomized micro-yields (`std::this_thread::yield()`) and microsecond delays.
- A stress suite spawns 16+ threads pounding locks and event notifications simultaneously under chaos jitter.

### Memory Guard Perturbation
- Randomizes allocation offsets within 16 KB page boundaries.
- Places `PAGE_NOACCESS` guard pages immediately before and after allocated guest blocks to catch out-of-bounds reads/writes instantly.

### Differential Testing
- `libc` and `libkernel` string, math, formatting, and file behavior are verified differentially against host C++ standard library and FreeBSD 12 reference implementations.

---

## 5. Toolchain & Multi-Agent Caching (ccache)

To avoid recompiling 3rdparty dependencies (`SDL2`, `SPIRV-Tools`, `glslang`, `googletest`) across multiple worktrees and AI agents, `ccache` can be configured globally in `ccache.conf`:

```ini
base_dir = <parent-worktree-directory>
hash_dir = false
max_size = 20.0 GB
sloppiness = include_file_ctime, include_file_mtime, time_macros
```

- When any agent compiles a submodule file in any worktree, all other agents get an instant cache hit (~0.001s).
- Submodules should be initialized using `--reference` to share git objects across worktrees:
  ```powershell
  git submodule update --init --recursive --reference <path-to-reference-clone>
  ```

---

## 6. How to Run Tests Locally

```powershell
# 1. Build development preset
cmake --build --preset dev

# 2. Run all unit tests
ctest --preset unit

# 3. Run a specific test suite or test case
ctest --preset unit -R SyncOnAddress
# Or directly via GTest filter:
./build/dev/tests/kernel_tests.exe --gtest_filter=SyncOnAddressTest.*
```
