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

Reference open-source implementations provide battle-tested low-level test cases that are systematically adapted into PortPS5 GoogleTest suites. All Title IDs are scrubbed to generic synthetic mocks (`PPSA00000`) per legal and policy rules.

### 3.1 KytyPS5 Subsystem Test Library
KytyPS5 provides extensive C++ subsystem test suites covering core kernel primitives, 16 KB virtual memory management, and compute shader reconstruction:

| Reference Test Source | Target PortPS5 Suite | Key Coverage |
| :--- | :--- | :--- |
| `SyncOnAddressTests.cpp` | `tests/kernel/SyncOnAddressTests.cpp` | `Wait32`, `Wait64`, `Wake`, misalignment rejection, timeouts, concurrent wakeups. |
| `EventQueueLifetimeTests.cpp` | `tests/kernel/EventQueueLifetimeTests.cpp` | Event queues, filters (`EVFILT_READ`, `EVFILT_TIMER`, `EVFILT_USER`), duplicate events, concurrent destruction. |
| `VirtualMemoryAllocationTests.cpp` | `tests/memory/VirtualMemoryAllocationTests.cpp` | 16 KB pages, memory protection flags, memory pools, direct memory mapping, fibers, red zone patching. |
| `MemoryTrackerTests.cpp` | `tests/memory/MemoryTrackerTests.cpp` | Partial port onto `GuestMemoryTracking::Watch`: range validation, page rounding, protection and fault resolution, invalidate, resolver contract (death tests), concurrency. Kyty's dirty-ownership upload/download model and `RangeSet` are not ported. |
| `KernelFileSystemTests.cpp` | `tests/kernel/KernelFileSystemTests.cpp` | Descriptor renames while open, mount point isolation (`/savedata0`), path canonicalization. |
| `SaveDataMemoryTests.cpp` | `tests/kernel/SaveDataMemoryTests.cpp` | User slots, memory setup, crash-safe save mounts, unmount safety. |
| `SaveDataMountTests.cpp` | `tests/filesystem/SaveDataMountTests.cpp` | `/savedata0` mount: create/write/fsync/read round trip, mkdir, `..` and `:` containment (EACCES), unmounted fails ENOENT, title id sanitising. |
| `AudioOut2PortTests.cpp` | `tests/audio/AudioOut2PortTests.cpp` | Audio ports, PCM streaming, volume, rerouting, device open/close state machines. |
| `PadHapticsTests.cpp` | `tests/input/PadHapticsTests.cpp` | DualSense rumble and haptics emulation with mocked SDL streams. |
| `ShaderRecompilerComputeTests.cpp` | `tests/shader/ComputeInstructionsTest.cpp` | Full RDNA2 compute instruction set verification. |
| `shaderCfgTests.cpp` | `tests/shader/ShaderCfgTest.cpp` | CFG reconstruction, SSA rewrite, dead code elimination, constant propagation, SPIR-V validation. |

### 3.2 SharpEMU Security, Concurrency & Instruction Edge-Case Tests
SharpEMU (GPL-2.0-or-later) provides targeted verification for guest sandbox containment, POSIX threading semantics, fibers, and specific RDNA2 ALU instructions:

| SharpEMU Test Suite | Target PortPS5 Suite | Key Coverage |
| :--- | :--- | :--- |
| `KernelSandboxEscapeTests.cs` | `tests/filesystem/SandboxPathTests.cpp` | Guest-to-host path containment: default-deny on unmapped absolute paths (`/etc/passwd`, `/proc/self/mem`, UNC shares), mount-relative `..` path traversal containment (`/app0/../../`), and case-sensitivity normalization. |
| `PthreadMutexSemanticsTests.cs` | `tests/kernel/PthreadMutexTests.cpp` | Normal, recursive, and error-checking mutex semantics, priority ceiling, timeouts, owner destruction, and waiter handoff. |
| `PthreadCondSemanticsTests.cs` | `tests/kernel/PthreadCondTests.cpp` | Clock selection (`CLOCK_MONOTONIC` vs `CLOCK_REALTIME`), timed waits, spurious wakeup handling, and signal/broadcast dispatch. |
| `PthreadRwlockSemanticsTests.cs` | `tests/kernel/PthreadRwlockTests.cpp` | Reader reentrancy, writer-preference priority, and writer starvation prevention. |
| `FiberExportsTests.cs` & `FiberSwitchLoopTests.cs` | `tests/kernel/FiberTests.cpp` | `sceFiberInitialize`, `sceFiberSwitch`, `sceFiberReturnToThread`, and register preservation across fiber yields. |
| `Gen5*Tests.cs` (Alu, Pack, Xor) | `tests/shader/Rdna2InstructionTests.cpp` | Packed half-precision math (`VopcF16`, `SignedPack16`), bit manipulation (`ThreeInputXor`, `XorAdd`, `SignedMultiply24`), and LDS data sharing (`DataShareRead64`, `DataShareSwizzle`). |
| `VideoOutFlipStatusTests.cs` & `VideoOutLatencyTests.cs` | `tests/video/VideoOutTests.cpp` | Presentation flip queue state transitions, latency calculation, and pixel format conversions. |
| `KernelAioWaitTests.cs` & `KernelPosixSocketOptionTests.cs` | `tests/kernel/KernelNetworkAioTests.cpp` | Kernel AIO completion wait queues and POSIX socket option handling (`SO_REUSEADDR`, non-blocking I/O). |

### 3.3 FreeBSD Official Test Suite (Kernel & Libc)
Because the PS5 kernel and userland are derived from FreeBSD 12, FreeBSD’s official test suite (`lib/libc/tests` and `sys/tests`) provides authoritative verification:

| FreeBSD Test Suite | Target PortPS5 Suite | Key Coverage |
| :--- | :--- | :--- |
| `sys/kqueue/` (`kqueue_test.c`) | `tests/kernel/KqueueEventTests.cpp` | `kqueue`/`kevent` semantics underlying `sceKernelCreateEqueue`: multiple concurrent waiters, user triggers (`EVFILT_USER`), timer intervals (`EVFILT_TIMER`), socket readiness (`EVFILT_READ`/`WRITE`), and EOF delivery. |
| `sys/kern/` (`umtx_test.c`) | `tests/kernel/UmtxSyncTests.cpp` | `umtx` futex sleep/wake semantics, priority inheritance, lock handoff, and timeout accuracy under CPU preemption. |
| `sys/vm/` (`mmap_test.c`, `mprotect_test.c`) | `tests/memory/PosixVmTests.cpp` | 16 KB page boundary rounding, `MAP_FIXED`, `MAP_ANON`, `PROT_NONE/READ/WRITE/EXEC` transition matrix, and partial unmapping. |
| `lib/libc/` (`stdio_test.c`, `string_test.c`, `gen/`) | `tests/libc/PosixLibcTests.cpp` | `readv`, `writev`, `fstat`, path resolution, non-blocking socket pairs, locale-independent formatting, and edge-case libc corner cases. |

### 3.4 Mesa ACO (AMD RDNA2 GFX10.3 Shader Compiler Tests)
Valve’s ACO compiler in Mesa (`src/amd/compiler/tests/`) provides the industry reference test vectors for AMD RDNA2 (Navi 2x / GFX10.3):

| ACO Test Suite | Target PortPS5 Suite | Key Coverage |
| :--- | :--- | :--- |
| `test_insert_NOPs.cpp`, `test_assembler.cpp` | `tests/shader/AcoInstructionEncodingTests.cpp` | Full bit-accurate RDNA2 instruction decoding: VOP1, VOP2, VOP3, VOPC, SOPK, SMEM, and scalar memory reads. |
| `test_optimizer.cpp` | `tests/shader/AcoDppSdwaTests.cpp` | Data Parallel Primitives (DPP) lane swizzling, Sub-Dword Addressing (SDWA) bit packing, and float16 conversion modes. |
| `test_divergent_control_flow.cpp` | `tests/shader/AcoControlFlowTests.cpp` | Exec mask save/restore (`s_and_saveexec`), divergent branch reconvergence, break/continue structurization, and loop phi resolution. |
| `test_lds_atomics.cpp` | `tests/shader/AcoLdsAtomicsTests.cpp` | 32-bit and 64-bit Local Data Share (LDS) read/write, shared memory barriers, and atomic memory operations. |

### 3.5 Wine / Proton Conformance Tests (Windows Host & Relinker)
Wine’s test suites (`dlls/*/tests`) verify Windows API translation and PE relinking edge cases:

| Wine Test Suite | Target PortPS5 Suite | Key Coverage |
| :--- | :--- | :--- |
| `dlls/ntdll/tests/sync.c` | `tests/kernel/NtdllSyncTests.cpp` | `WaitOnAddress`, `WakeByAddressSingle`, `WakeByAddressAll`, keyed event behavior, and lock fairness under heavy thread contention. |
| `dlls/ntdll/tests/virtual.c` | `tests/memory/NtdllVirtualMemoryTests.cpp` | `VirtualAlloc`, `VirtualProtect`, guard pages (`PAGE_GUARD`), write tracking (`GetWriteWatch`), and memory reset behavior. |
| `dlls/kernel32/tests/path.c` | `tests/filesystem/WindowsPathTests.cpp` | Path canonicalization, long paths (`\\?\`), relative path navigation, and case-insensitivity on Windows host filesystems. |
| `dlls/ntdll/tests/relay.c` & `loader.c` | `tests/relinker/PeRelocationTests.cpp` | PE section alignment, TLS callback execution, and base relocation application. |

### 3.6 shadPS4, RPCS3 & DXVK (Containers, Media & Vulkan Caches)
Specialized emulator and graphics layer tests covering Sony container formats, audio decoders, and Vulkan caches:

| Project & Suite | Target PortPS5 Suite | Key Coverage |
| :--- | :--- | :--- |
| **shadPS4** (`tests/`) | `tests/filesystem/SfoContainerTests.cpp` | Param SFO parsing, PFS package header verification, and user-space heap allocation (`mspace`). |
| **RPCS3 / LibAtrac9** (`tests/`) | `tests/audio/Atrac9ConformanceTests.cpp` | Conformance test bitstreams for ATRAC9 audio decoding, grain processing, and multi-channel PCM ring-buffer queuing. |
| **DXVK** (`tests/dxvk/`) | `tests/video/VulkanBufferCacheTests.cpp` | Vulkan buffer cache range tracking, partial buffer updates, staging buffers, and render-target detiling. |

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
