# PortPS5 — Spec: Threading and synchronization

Status: draft v1 · 2026-09-27

## Scope

The libkernel synchronization surface: `scePthread*` and POSIX `pthread_*` (mutexes, condition variables, rwlocks, once, TSD); event flags, semaphores and `_umtx_op`; `libSceFiber` as it interacts with locks; thread creation, stacks, TLS, priority and affinity; the scheduler tick, sleeps and clocks; and the error policy for these calls.

Paths are relative to `core/libs/prx/`. "main" means `e06dbff`; "PR5" means `29b4601`.

## Current state

| Area | main | PR #5 |
|---|---|---|
| Guest object | Each guest sync object is an 8-byte slot that holds a host pointer (`SceTypes.hpp:162-173`). | Same. |
| Mutex | `PthreadMutexPrivate` wraps both a `recursive_timed_mutex` and a `timed_mutex` (`libkernel/Pthread/include/Pthread.hpp:24-32`). `resolveMutex` takes the process-global `initializationMutex` on every lock, unlock and trylock (`src/Mutex.cpp:12,21,138,142,161`). Relocking an error-check mutex throws (`Mutex.cpp:41`). | There is **no global lock** in the sce layer. It returns `EDEADLK` and `EPERM` (`src/Mutex.cpp:64-97`). A null slot throws (65). The POSIX wrappers lazily initialize the static initializers 0 and 1 by CAS; the default type is ErrorCheck (`Posix/Mutex.cpp:34-48,65`). Timed locks poll `try_lock_for` (`Time/include/TimedWait.hpp:23-38`). |
| Condition variable | `condition_variable_any` behind the global `condInitializationMutex` (`src/Cond.cpp:17-25,129-140`). | `TimedWait::Condition` is a FIFO waiter queue under a `queueLock`, with one Win32 event per waiter (`TimedWait.hpp:42-105`, `Time/TimedWait.cpp:63,156-178`). `Signalto` degrades to `NotifyAll` (`src/Cond.cpp:90-95`). |
| Rwlock, sema, event flag | Rwlock: a std wrapper. Sema: `std::mutex` plus `condition_variable` (`Semaphore/include/Semaphore.hpp:18-22`). Event flags: every call is `NotImplemented` (`EventFlag/src/EventFlag.cpp:8-35`). | Rwlock is a `shared_timed_mutex` (`Pthread.hpp:40-43`). Sema and event flags use a mutex plus `TimedWait::Condition`. Single-waiter event flags return `EPERM` (`EventFlag.cpp:30-39,128,142`). |
| TSD, once | TSD is `NotImplemented` (`src/Tsd.cpp:6-30`). | There are 512 keys. `Getspecific` is lock-free; `Setspecific` takes `g_keyLock` (`src/Tsd.cpp:10-21,79-88`). `scePthreadOnce` uses `atomic_ref` wait/notify (`src/Thread.cpp:346-360`). |
| `_umtx_op` | Absent. | Absent (no match in the tree). |
| Threads | `_beginthreadex` with the guest stack size (`src/Thread.cpp:158`). | Same (`Thread.cpp:166`), plus a stack-commit check (105-128). Affinity and priority are only stored (320-344). `scePthreadGetthreadid` returns the Win32 TID (300-306). The job-worker name match `"BPE JobWorkerThread"` pins threads behind `APS5_*` switches (67-95). That is **title-specific**. |
| TLS | The relinker rewrites the guest `mov rax, fs:[0]` into a stub that reads the PE TLS slot through `gs:[0x58]` (`relinker/elfpatcher/src/windows/WindowsTlsBuilder.cpp:82,118`). | Same. |
| Fibers | Stubs. | An asm context switch in `libSceFiber/Export.cpp:98`. Fibers migrate between threads (34-37). |
| Time | — | `NtSetTimerResolution` is set to 0.5 ms at load, with power throttling disabled (`libkernel/Time/Time.cpp:71-89`). `SleepUntil` waits on a high-resolution timer and then spins `YieldProcessor` for the last 0.5 ms (`TimedWait.cpp:231-244`). `APS5_TIME_SCALE` rescales the guest clocks (`Time.cpp:39-45`). |
| Errors | `NotImplemented_nid_no_patch` logs and aborts via `Unsupported()` (`libc/src/General.cpp:86-91`); it no longer throws, because the shared unwinder let guest `catch(...)` swallow host exceptions. | Same (`General.cpp:85-87`). |

## Decision

Per the decision table in [README.md](README.md#subsystem-specs), **replace** the primitives with futex words stored in place and lazily initialized by CAS, on `WaitOnAddress`. `_umtx_op`, event flags and semaphores share that one primitive. Real errors become return codes, and `Unsupported()` aborts.

Adopt from PR5 its error codes, POSIX static-initializer semantics, TSD, fibers and the raised tick.

Delete the job-worker affinity hack and `APS5_TIME_SCALE`. Both change behaviour, so neither may live in `[debug]`.

## Target design

**Guest thread ids.** Each thread gets a compact tid in `[1, 2^24)` on first entry. That covers guest threads and host threads that call into the guest, such as driver workers. The tid lives in a `thread_local` and is recycled after join or detach-exit. It is the owner field in every word below and the value `scePthreadGetthreadid` returns.

**Futex core.** Every wait loops on `WaitOnAddress(addr, &expected, 8|4, ms)` and re-checks after each return, so spurious wakeups are allowed. Wakes use `WakeByAddressSingle` or `WakeByAddressAll`. Deadlines are QPC nanoseconds, and waits of 1 ms or more pass the millisecond floor of the remaining time. Under 1 ms, the loop re-checks the word in 100 µs high-resolution-timer slices, spinning at most 50 µs. This replaces PR5's 0.5 ms spin.

**Mutex word** (the whole 8-byte guest slot, little-endian, all accesses 64-bit atomics):

```
bit 63 INIT | 62 DESTROYED | 58..56 type (1 errchk, 2 recursive, 3 normal) | 47..32 recursion-1 | 31 CONTENDED | 23..0 owner tid
slot == 0 -> static default (ErrorCheck, as PR5 Posix/Mutex.cpp:45)   slot == 1 -> adaptive (Normal)
```

Canonical user pointers never set bit 63, so INIT words cannot collide with the old pointer representation.

- **Lock:** `CAS(INIT|type|0 → INIT|type|tid)`. The first lock of a static slot does `CAS(0 → INIT|ErrorCheck|tid)`, which is lazy initialization and acquisition in one instruction.
- **Contention:** a Drepper-style three-state lock. Waiters set CONTENDED, then wait on the slot. Unlock clears the owner, and wakes one waiter only if CONTENDED was set.
- **Recursive locks:** the owner increments bits 47..32. Overflow returns `EAGAIN`.
- **Error cases:** relocking an error-check mutex returns `EDEADLK`. Unlocking a mutex owned by another thread returns `EPERM`. Destroying a locked mutex returns `EBUSY`. Any use after destroy returns `EINVAL`.

**Condition variable word:**

```
bit 63 INIT | 62 DESTROYED | 61 CLOCK_MONOTONIC | 47..32 waiters | 31..0 seq
```

- **Wait:** read `seq`, increment `waiters`, unlock the mutex, then wait while `seq` is unchanged. A wait returns when `seq` changes; if some other bit changed, it waits again with the new value. Then it relocks the mutex *in CONTENDED state*, so the next unlock hands off.
- **Signal:** does nothing when `waiters == 0`. Otherwise it increments `seq`, decrements `waiters` and wakes one.
- **Broadcast:** increments `seq`, sets `waiters = 0` and wakes all. There is no requeue in v1.
- `Signalto` stays a broadcast, which POSIX permits as a spurious wakeup.

**Rwlock word:**

```
63 INIT | 62 DESTROYED | 61 WRITER | 60 WRITERS_WAITING | 59 READERS_WAITING | 55..32 writer tid | 29..0 readers
```

Writers are preferred once `WRITERS_WAITING` is set. A writer that relocks gets `EDEADLK`. The reader count saturates at 2^30 and then returns `EAGAIN`.

**Handle objects.** Semaphores and event flags are created by call, so they keep a host object in the handle. The object holds futex words instead of a mutex and condition:

```cpp
struct Sema { std::atomic<i32> count; i32 max; std::atomic<u32> waiters; bool fifo; FifoQueue* q; };
struct EventFlag { std::atomic<u64> pattern; std::atomic<u64> epoch; // bumped by set/cancel/delete
                   std::atomic<u32> waiters; bool multi; };
```

- **Event flag wait:** a CAS loop on `pattern` implements the AND/OR checks and the CLEAR_ALL/CLEAR_PAT modes. Waiters sleep on `epoch`. Set does `fetch_or`, bumps `epoch` and wakes all when waiters are present. Cancel and delete bump `epoch` and store a status the woken waiters read.
- **FIFO semaphores:** keep PR5's FIFO order through an intrusive queue of per-waiter futex words, guarded by an internal futex spinlock. Non-FIFO semaphores are a pure counter futex.

**`_umtx_op`.** This follows the public FreeBSD semantics:

| Operation | Mapping |
|---|---|
| `WAIT` (long), `WAIT_UINT`, `WAIT_UINT_PRIVATE` | `WaitOnAddress` of size 8 or 4 |
| `WAKE`, `WAKE_PRIVATE`, `NWAKE_PRIVATE` | wake-N |
| `MUTEX_*` | the `umutex` owner word, with `UMUTEX_CONTESTED` as bit 31 |
| `CV_*`, `RW_*`, `SEM2_*` | the same algorithms as above, over the guest structs |

An unknown operation returns `EINVAL` and is logged once per operation.

**Threads, TLS, scheduling.**

- **Creation:** `_beginthreadex` stays, with a committed guest-size stack and a guard page.
- **TLS:** unchanged from the relinker path ([relinker.md](relinker.md)). Fibers share their host thread's TLS and tid, and a mutex owned before a fiber migrates stays owned by the old tid (inference: this matches PS5 semantics, where the owner is the thread).
- **Priority:** mapped onto five Win32 bands, with no realtime class.
- **Affinity:** recorded, not applied. The guest masks name console cores, not host cores. This spec owns the rule; other specs refer to it here.
- **Timer:** the 0.5 ms tick raise is kept unconditionally.
- **`sceKernelUsleep(0)` and `scePthreadYield`:** both call `SwitchToThread`.

**Error policy.**

- Real POSIX and SCE conditions return codes: null pointer → `EINVAL`, plus `EBUSY`, `ETIMEDOUT`, `EDEADLK`, `EPERM`, `EAGAIN`, `ESRCH`.
- A state that is truly unimplemented calls `[[noreturn]] Unsupported(const char* what)`. It logs the NID, caller offset and thread name, then calls `abort()`. It replaces the `throw` in `NotImplemented_nid_no_patch`, which the shared unwinder lets a guest `catch(...)` swallow.
- Host exceptions never cross an `APS5_VABI` boundary. Every export body is `noexcept`, enforced by the export macro.

## Interfaces

| Spec | Contract |
|---|---|
| [guest-memory.md](guest-memory.md) | Pin waits use this futex core. Registry and tracker locks are host locks, never held across a guest wait. |
| [gpu-driver.md](gpu-driver.md) | Driver worker threads get guest tids on first callback. The GPU lock is a host lock, never a guest word. Label waits use `WaitOnAddress` on guest memory. |
| [audio.md](audio.md), [video-fmv.md](video-fmv.md) | Mixer and decode threads sleep through `TimedWait` deadlines. A/V pacing depends on the 0.5 ms tick. |
| [input.md](input.md), [save-data.md](save-data.md) | Callback threads use the same tid and error rules. |
| [configuration.md](configuration.md) | Only `[debug]` tracing replaces `APS5_TRACE_SYNC`, `APS5_TRACE_WAITS` and `APS5_TRACE_USLEEP`. `APS5_COARSE_TIMED_WAITS` and the job-affinity switches are removed. |
| [build-toolchain.md](build-toolchain.md) | Needs `Synchronization.lib` (`WaitOnAddress`) and C++23 `atomic_ref`. |
| [verification.md](verification.md) | The futex sync tests run in the hosted `unit` job. The watchdog's "guest thread progress" signal is a per-tid heartbeat counter kept here. |

## Failure modes

| Failure | Handling |
|---|---|
| Guest copies or `memcpy`s an initialized sync object | Undefined in POSIX. The copied word still works if it was unlocked. Detected only by tests. |
| Guest inspects the slot expecting a pointer | Open question 1. The fallback is a pointer mode: an INIT-less side object, selected per mechanism in `[workarounds]`. |
| Lost wakeup | Prevented by re-checking the word after every wake. The stress tests below guard against regressions. |
| A deadlock (guest bug or ours) | The watchdog records a softlock. With `debug.threading.dump_futex_owners` ([configuration.md](configuration.md)), it also logs the owner tid of every contended word it has seen. |
| More than 2^24 live threads, or recursion overflow | `EAGAIN`. |
| `WaitOnAddress` millisecond timeout granularity | Sub-millisecond slices, as above. Timed-wait error is measured in the tests. |
| Unimplemented `_umtx_op` operation, or an unimplemented export | Return `EINVAL`, or `Unsupported()` when no code is meaningful. Never a throw. |

## Tests

- **GoogleTest Unit Suites** (`ctest -L unit`, hosted `unit` job):
  - Every mutex type (lock, trylock, timedlock, `EDEADLK`, `EPERM`, `EBUSY`, destroy).
  - Lazy initialization with 64 threads racing on a zero slot, with exactly one INIT winner.
  - Condition-variable 10^6-round ping-pong and a broadcast storm with no lost wakeups.
  - RWLock writer preference and recursive read lock semantics.
  - Event-flag AND/OR and clear modes, single-waiter `EPERM` and cancel.
  - Semaphore FIFO ordering and count clamping.
  - `_umtx_op` wait/wake at sizes 4 and 8, ported from FreeBSD 12 `sys/tests` umtx validation.
  - TSD destructors at thread exit and `once` under multi-threaded contention.
  - Death tests (`EXPECT_DEATH`): verify that `Unsupported()` aborts the process and cannot be caught by guest C++ `catch(...)` exception handlers.
- **Ported Ecosystem Test Suites:**
  - **KytyPS5 `SyncOnAddressTests`:** wait-on-address primitives, wake-all broadcast storms, spurious wakeup resilience, sub-millisecond timeout slice accuracy.
  - **SharpEMU `Pthread*SemanticsTests`:** POSIX mutex attribute invariants, timed condvar deadline precision, writer-preference rwlock starvation prevention.
  - **SharpEMU `Fiber*Tests` (M4):** fiber stack allocation, context switching, migration across threads, and fiber-local storage (FLS) isolation.
  - **Wine / Proton Concurrency Perturbation:** high-contention `WaitOnAddress` race conditions under thread affinity perturbation.
- **Microbenchmarks** (M1 exit): an uncontended lock/unlock pair is **at least 10× faster** than main's implementation. Also measured: contended hand-off latency, condition-variable round trip, `sceKernelUsleep(100)` error (p50/p99), and the CPU cost of a 1 ms sleep.
- **Local regression:** per-run telemetry counts contended waits, `Unsupported()` hits (target 0) and watchdog heartbeats. Bugsnax job-system coverage is checked at M4.

## Milestones

| Milestone | Delivers |
|---|---|
| M1 | - [ ] The futex rewrite of mutex, condition variable, rwlock and `_umtx_op`, with no global mutex. Correct errno and SCE returns. `Unsupported()` in place of throws. Compact tids. Removal of the job-affinity and time-scale switches. The sync microbenchmark. (ROADMAP M1) |
| M4 | - [ ] Event flags, semaphores and fibers on the shared primitive, for UE4 job-system coverage. (ROADMAP M4) |
| M5 | - [ ] The perf pass over contention and sleep telemetry. (ROADMAP M5) |

## Open questions

1. Does any gate-title code read the mutex or condition slot as a pointer, for example `if (*m == NULL)`? The M1 NID inventory and a guarded trial run decide whether pointer mode is ever needed.
2. Is `_umtx_op` imported by any gate title directly, or only through our libkernel? This affects priority, not the design.
3. Does FIFO order matter for anything beyond FIFO-attributed semaphores? For example, would broadcast requeue avoid a thundering herd in job systems?
4. Should the guest priority bands map to Windows priorities at all, or would that starve the presenter and driver threads?
5. Raw-port status (PR #28, not yet adapted): `libkernel/Time/TimedWait.cpp` (+ `include/TimedWait.hpp`), `libkernel/Pthread/Posix/Common.hpp` and `libkernel/Apr/src/Apr.cpp` (+ `AprCommandBuffer.hpp`) are byte-identical to upstream `53bda68`. Debt: `throw std::runtime_error` sites in `Apr.cpp` must become SCE return codes or the logging abort path; `Apr.cpp` host file access (`std::filesystem`/`std::fstream`) needs a sandbox review against [save-data.md](save-data.md); the ported `tests/Fiber.cpp` and `tests/GuestCondTimedwait.cpp` use `abort()`/`main()` and must become GTest via `portps5_add_gtest` (fibers gate M4).
