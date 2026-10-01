# PortPS5 — Spec: Host platform layer

Status: draft · 2026-10-01

## Scope

The host operating-system services that the runtime libraries use: virtual memory (reserve, commit, protect, aliased views), write tracking backends, futex wait and wake, threads and host TLS, fault handling, monotonic clocks, files and paths, and loading of the runtime libraries. Windowing, audio devices and input are out of scope, because SDL2 already abstracts them ([input.md](input.md), [audio.md](audio.md)). Vulkan is portable by itself.

The layer exists so that native Linux (v2, ROADMAP M7) is a new backend and not a rewrite. Windows stays the only 1.0 release target (PRD §5), and MinGW-w64 GCC 15.2 stays the only Windows compiler ([build-toolchain.md](build-toolchain.md)).

## Current state

PortPS5 `main` on 2026-10-01:

- There is no layer. Win32 calls sit at their call sites: `WaitOnAddress` in 11 files under `core/` (the futex core in `libkernel/Pthread`, the AGC `Sync.cpp` files, the audio mixer, a test); `GetWriteWatch` in `libc` `WriteTracker` and `MemoryTrackingWindows.cpp`; vectored exception handling in one file. `VirtualAlloc2` and `MapViewOfFile3` are not used yet. The M5 aliasing design names them ([guest-memory.md](guest-memory.md)).
- Linux code inherited from AnyPS5 exists but isn't built in CI:
  - the relinker's Linux ELF output (`core/relinker/elfpatcher/src/linux/LinuxElfPatcher.cpp`, `WriteLinux`; [relinker.md](relinker.md) open question 5);
  - `libc/src/specifics/linux/` (`DlIteratePhdr.cpp`);
  - `__linux__` branches in `SymbolAlias.hpp`, `Unwind.cpp`, `DirectMemory.cpp` and `NativeStat.cpp`.
  
  In all, 75 files under `core/` contain `_WIN32` or `__linux__` guards.
- Hosted CI builds C++ only on `windows-2022`. The Ubuntu runners run change detection and the Python quality job.
- AnyPS5 has a Linux `userfaultfd` write-tracking backend that isn't ported (bean `portps5-u5fe`).

## Decision

- Add `core/host/` with one interface per service and one backend per OS, starting with `core/host/win32/`. A backend is chosen at build time, never at run time.
- **v1 rule (seam only):** new code calls `core/host/`, not Win32. Existing Win32 call sites move into the Win32 backend when a change touches them. There is no big-bang migration, and moving a call is not a behaviour change.
- A `policy` check keeps an allowlist of files outside `core/host/` that include `<windows.h>` or call Win32 directly. The allowlist may only shrink.
- Proton/Wine on Linux is a best-effort smoke test of the Windows build in v1, never a release target.
- Native Linux is v2 (ROADMAP M7): a `core/host/linux/` backend, ELF output from the relinker, and a hosted Linux CI build.

## Target design

| Service | Interface (proposal) | Win32 backend | Linux backend (v2) |
|---|---|---|---|
| Virtual memory | `Reserve`, `Commit`, `Protect`, `Release`, `MapView(physical, offset, size, address)` | `VirtualAlloc`, placeholders through `VirtualAlloc2` / `MapViewOfFile3` (M5) | `mmap` with `MAP_FIXED_NOREPLACE`, `mprotect`; aliased views from one `memfd` mapped several times |
| Write tracking | the existing `IWriteTracker` backends ([guest-memory.md](guest-memory.md)) | `MEM_WRITE_WATCH` with `GetWriteWatch` | `userfaultfd` write-protect with `PAGEMAP_SCAN` (bean `portps5-u5fe`) |
| Futex | `Wait(address, expected, timeout)`, `WakeOne`, `WakeAll` | `WaitOnAddress`, `WakeByAddress*` | `futex(2)` `FUTEX_WAIT_PRIVATE` / `FUTEX_WAKE_PRIVATE` |
| Threads and TLS | create with stack size, name, host TLS slot | `_beginthreadex`, `TlsAlloc` | `pthread_create`, `pthread_key_create` |
| Faults | register a handler for access violations on guest ranges | vectored exception handler | `sigaction(SIGSEGV)` with an alternate signal stack |
| Clock | monotonic ns and its frequency | `QueryPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)` |
| Files | open, positional read/write without a shared offset, stat, directory iteration, path mapping | `CreateFileW` with `OVERLAPPED` offsets | `openat`, `pread`/`pwrite`, `fstatat` |
| Runtime libraries | load a prx and resolve its exports | PE DLLs | ELF shared objects |

- Every interface returns codes and doesn't throw. Each OS backend owns the mapping from host errors to POSIX/SCE codes.
- The ABI stays as it is. On Linux the host already uses the System V ABI, so `APS5_VABI` expands to nothing and every export keeps `APS5_EXPORT_FN`.
- **Guest TLS on Linux** is the main open risk: guest code addresses its TLS through the `fs` segment, and glibc uses `fs` for host TLS. It needs a spike before M7 commits (open question 1).
- **File positional I/O** is the seam for the async request queue of [threading.md](threading.md) open question 7 (`sceKernelAio*`, bean `portps5-j4e1`). In v2, the streaming and decompression pipeline (ROADMAP M8) sits on the same queue.

## Interfaces

| Consumer | Uses |
|---|---|
| [threading.md](threading.md) | Futex, threads and TLS, clock. |
| [guest-memory.md](guest-memory.md) | Virtual memory, aliased views, write tracking, faults. |
| [gpu-driver.md](gpu-driver.md) | Futex (queue sync), clock (telemetry timing). |
| [save-data.md](save-data.md), libkernel `File` | Files and path mapping. |
| [relinker.md](relinker.md) | Picks the output format (PE or ELF) to match the backend. |
| [verification.md](verification.md) | Clock for telemetry, and the `policy` allowlist check. |

## Failure modes

| Failure | Behaviour |
|---|---|
| The host refuses a reservation or view at the requested address. | Return the SCE error that the guest call defines. Never retry at a different address without the guest asking. |
| Fault in a guest range with no tracker registration. | Re-raise to the crash path. Never swallow it. |
| Fault on the Linux signal stack while a host lock is held. | The handler takes no locks and only updates lock-free page state (same rule as the Win32 handler). |
| A backend lacks a service (for example `PAGEMAP_SCAN` on an old kernel). | Fall back to the documented slower path when one exists, logged once. Otherwise go through `Unsupported()`. |

## Tests

- [ ] Interface contract suites (GoogleTest, `portps5_add_gtest`), written once and run against every backend: futex wake and timeout, reserve/commit/protect transitions, two views of one physical range seeing each other's writes, positional reads that don't move a shared offset.
- [ ] The `policy` job checks the Win32 allowlist can only shrink.
- [ ] Proton smoke run of the Windows test binaries (v1, best effort, local or scheduled; it never blocks a PR).
- [ ] v2: a hosted Linux CI build and `ctest` with the same suites.

## Milestones

| Milestone | Delivers |
|---|---|
| M2–M3 (v1 seam) | - [ ] `core/host/` interfaces, the Win32 backend for futex, clock and virtual memory, contract tests, and the `policy` allowlist (bean `portps5-37j0`). - [ ] Proton smoke test of the Windows build (bean `portps5-qfac`). |
| M5 (v1 seam) | - [ ] Aliased views through `MapView` for direct-memory aliasing (bean `portps5-r2ns`). |
| M7 (v2) | - [ ] TLS spike (bean `portps5-u16x`). - [ ] Linux backend for every service, relinker ELF output in CI, hosted Linux build, and v1 gate titles on Linux (ROADMAP M7). |

## Open questions

1. Guest TLS on Linux: does guest code use `fs:` directly, or only through our libc's `__tls_get_addr`? If directly, are the options switching `fs` base on guest entry (`arch_prctl`, or `wrfsbase` where the kernel allows it), relinker rewriting of `fs:` accesses, or keeping glibc off `fs`? This decides whether M7 is feasible as planned.
2. Should the Linux runtime libraries stay NID-patched ELF shared objects as AnyPS5 builds them, or be linked into one host executable?
3. Does Wine/Proton implement `WaitOnAddress`, `MEM_WRITE_WATCH` and placeholder views well enough for the v1 smoke test, or does the smoke test have to skip write-tracking suites?
