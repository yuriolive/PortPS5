# PortPS5 — Spec: Threading and synchronization

Status: draft v1 · 2026-09-27 · synced with `main` 2026-10-01

## Scope

The libkernel synchronization surface: `scePthread*` and POSIX `pthread_*` (mutexes, condition variables, rwlocks, once, TSD); event flags, semaphores and `_umtx_op`; `libSceFiber` as it interacts with locks; thread creation, stacks, TLS, priority and affinity; the scheduler tick, sleeps and clocks; and the error policy for these calls.

Paths are relative to `core/libs/prx/`. "main@e06dbff" is the pre-merge AnyPS5 main (old baseline). "main@75a8668" is the current AnyPS5 main and includes merged PR #5; its line numbers were re-checked there.

## Current state

| Area | AnyPS5 main@e06dbff (pre-merge) | AnyPS5 main@75a8668 (incl. merged PR #5) |
|---|---|---|
| Guest object | Each guest sync object is an 8-byte slot that holds a host pointer (`SceTypes.hpp:162-173`). | Same. |
| Mutex | `PthreadMutexPrivate` wraps both a `recursive_timed_mutex` and a `timed_mutex` (`libkernel/Pthread/include/Pthread.hpp:24-32`). `resolveMutex` takes the process-global `initializationMutex` on every lock, unlock and trylock (`src/Mutex.cpp:12,21,138,142,161`). Relocking an error-check mutex throws (`Mutex.cpp:41`). | There is **no global lock on the lock and unlock path**: `resolveMutex` returns an initialized slot after an acquire load, and takes the process-global `initializationMutex` only to create, init or destroy a mutex (`src/Mutex.cpp:17,27-46,134,142`). It returns `EDEADLK` and `EPERM` (`src/Mutex.cpp:49-71,158-161`). Unlocking an uninitialized slot throws (42). The POSIX wrappers lazily initialize the static initializers 0 and 1 by CAS; the default type is ErrorCheck (`Posix/Mutex.cpp:21-35,52,88`). Timed locks poll `try_lock_for` (`Time/include/TimedWait.hpp:23-38`). |
| Condition variable | `condition_variable_any` behind the global `condInitializationMutex` (`src/Cond.cpp:17-25,129-140`). | `TimedWait::Condition` is a FIFO waiter queue under a `queueLock`, with one Win32 event per waiter (`TimedWait.hpp:42-105`, `Time/TimedWait.cpp:63-74,120-130,157-179`). `Signalto` degrades to `NotifyAll` (`src/Cond.cpp:147-151`). |
| Rwlock, sema, event flag | Rwlock: a std wrapper. Sema: `std::mutex` plus `condition_variable` (`Semaphore/include/Semaphore.hpp:18-22`). Event flags: every call is `NotImplemented` (`EventFlag/src/EventFlag.cpp:8-35`). | Rwlock is a `shared_timed_mutex` (`Pthread.hpp:40-43`). Sema and event flags use a mutex plus `TimedWait::Condition`. Single-waiter event flags return `EPERM` (`EventFlag.cpp:30-39,128,142`). |
| TSD, once | TSD is `NotImplemented` (`src/Tsd.cpp:6-30`). | There are 512 keys. `Getspecific` is lock-free; `Setspecific` takes `g_keyLock` through `IsValidKey` (`src/Tsd.cpp:10,20,50-54,79-88`). `scePthreadOnce` uses `atomic_ref` wait/notify (`src/Thread.cpp:424-439`). |
| `_umtx_op` | Absent. | Absent (no match in the tree). |
| Threads | `_beginthreadex` with the guest stack size (`src/Thread.cpp:158`). | Same (`Thread.cpp:217`), plus a stack-commit check (158-169). Affinity and priority are only stored (204-205,357-420). `scePthreadGetthreadid` returns the Win32 TID (378-384). The job-worker name match `"BPE JobWorkerThread"` pins threads behind `APS5_*` switches (118-146). That is **title-specific**. |
| TLS | The relinker rewrites the guest `mov rax, fs:[0]` into a stub that reads the PE TLS slot through `gs:[0x58]` (`relinker/elfpatcher/src/windows/WindowsTlsBuilder.cpp:82,118`). | Same, re-checked: the stub reads the slot through `gs:[0x58]` (`WindowsTlsBuilder.cpp:118`) and the matched instruction forms are at 79-80. |
| Fibers | Stubs. | An asm context switch in `libSceFiber/Export.cpp:98` (second stub at 188). Fibers migrate between threads (32-34,73-74). |
| Time | — | `NtSetTimerResolution` is set to 0.5 ms at load, with power throttling disabled (`libkernel/Time/Time.cpp:71-89`). `SleepUntil` waits on a high-resolution timer and then spins `YieldProcessor` for the last 0.5 ms (`TimedWait.cpp:231-242`). `APS5_TIME_SCALE` rescales the guest clocks (`Time.cpp:39-45`). |
| Errors | `NotImplemented_nid_no_patch` throws `std::runtime_error` (`libc/src/General.cpp:86-88`), and the shared unwinder lets a guest `catch(...)` swallow host exceptions. | Same (`General.cpp:155-157`). |

**Status as of 2026-09-30 (PortPS5 `main`).** The M1 futex rewrite is in (PRs #19, #41, #43): mutex, condition variable, rwlock and `_umtx_op` on `WaitOnAddress` words with compact tids, return codes instead of throws, and the `SyncOnAddress`, `PthreadMutex`, `PthreadCond` and `EventQueueLifetime` suites under `tests/kernel/`. PR #61 (merged) added the futex-based pthread semaphore lifecycle, barriers, once, `sceKernelSleep`, `sigprocmask`, priority-protocol mutex attributes, adopted `scePthreadSelf` handles and `sceKernelDeleteSema`. Not landed: event flags, semaphores and fibers on the shared primitive (M4). Known libkernel gaps recorded as beans: POSIX `sem_*` stubs (`Posix/Sem.cpp`, bean `portps5-vfk7`), the `sceKernelAio*` request lifecycle (open question 7, bean `portps5-j4e1`), 64-bit `sceKernelLseek` (bean `portps5-65h0`), FIFO order for equal-priority semaphore waiters (open question 3, bean `portps5-ra5d`).

## Decision

Per the decision table in [README.md](README.md#subsystem-specs), **replace** the primitives with futex words stored in place and lazily initialized by CAS, on `WaitOnAddress`. `_umtx_op`, event flags and semaphores share that one primitive. Real errors become return codes, and `Unsupported()` aborts.

Adopt from AnyPS5 main (merged PR #5) its error codes, POSIX static-initializer semantics, TSD, fibers and the raised tick.

Delete the job-worker affinity hack and `APS5_TIME_SCALE`. Both change behaviour, so neither may live in `[debug]`.

## Target design

**Guest thread ids.** Each thread gets a compact tid in `[1, 2^24)` on first entry. That covers guest threads and host threads that call into the guest, such as driver workers. The tid lives in a `thread_local` and is recycled after join or detach-exit. It is the owner field in every word below and the value `scePthreadGetthreadid` returns.

**Futex core.** Every wait loops on `WaitOnAddress(addr, &expected, 8|4, ms)` and re-checks after each return, so spurious wakeups are allowed. Wakes use `WakeByAddressSingle` or `WakeByAddressAll`. Deadlines are QPC nanoseconds, and waits of 1 ms or more pass the millisecond floor of the remaining time. Under 1 ms, the loop re-checks the word in 100 µs high-resolution-timer slices, spinning at most 50 µs. This replaces AnyPS5 main's (merged PR #5) 0.5 ms spin.

**Mutex word** (the whole 8-byte guest slot, little-endian, all accesses 64-bit atomics):

```
bit 63 INIT | 62 DESTROYED | 58..56 type (1 errchk, 2 recursive, 3 normal) | 47..32 recursion-1 | 31 CONTENDED | 23..0 owner tid
slot == 0 -> static default (ErrorCheck, as AnyPS5 main@75a8668 `Posix/Mutex.cpp:32`)   slot == 1 -> adaptive (Normal)
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

- **Wait:** read `seq`, increment `waiters`, unlock the mutex, then wait while `seq` is unchanged. A wait returns when `seq` changes; if some other bit changed, it waits again with the new value. The unblocking waiter decrements `waiters` (ensuring exact 1:1 pairing with entry across spurious wakeups and timeouts). Then it relocks the mutex *in CONTENDED state*, so the next unlock hands off.
- **Signal:** does nothing when `waiters == 0`. Otherwise it increments `seq` and wakes one (unblocking waiter decrements `waiters`).
- **Broadcast:** increments `seq` and wakes all (unblocking waiters decrement `waiters`). There is no requeue in v1.
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
- **FIFO semaphores:** keep AnyPS5 main's (merged PR #5) FIFO order through an intrusive queue of per-waiter futex words, guarded by an internal futex spinlock. Non-FIFO semaphores are a pure counter futex.

**Handle objects ported from AnyPS5 (lane "kernel sync", futex-adapted).** These are created by call and keep a host object in the 8-byte slot, but the object holds futex words only (no heap `std::mutex` or condition variable):

- `scePthreadSem*`: `{atomic u32 count; atomic u32 waiters}` (`Pthread/src/Sem.cpp`, layout in `Pthread/include/SemBarrierTypes.hpp`). Waiters sleep on `count`; post publishes the count (seq_cst) before it reads `waiters`, so no wakeup is lost. Returns `EINVAL` (null, uninitialised or destroyed slot, non-zero `flag`, `value > INT_MAX`), `EAGAIN` (`Trywait` with no token; POSIX/FreeBSD, confirmed against shadPS4 and SharpEmu, not upstream AnyPS5's `EBUSY`), `EBUSY` (`Destroy` with sleeping waiters), `ETIMEDOUT`, `EOVERFLOW` (post at `INT_MAX`). Destroy leaves the marker value 2 in the slot.
- `pthread_barrier_*`: one 64-bit word `generation << 32 | arrived` plus an `active` counter (`Pthread/Posix/Barrier.cpp`). The arrival that reaches `count` publishes `(generation+1, 0)` in the same CAS, so a `count+1`-th caller lands in the next round. One thread per round returns `PTHREAD_BARRIER_SERIAL_THREAD` (-1). A non-null attr is accepted and ignored (pshared only).
- `scePthreadOnce` and `pthread_once` share one futex state-word implementation over the guest `once` control (`Pthread/Posix/Once.cpp`): claim by CAS 0 to 2, publish 1 with release, wake all. An initializer that unwinds resets the state to 0 so the next caller retries.
- `scePthreadSelf` on a host thread that never went through `scePthreadCreate` (the guest main thread, driver workers) returns a lazily adopted, detached handle (`adopted = true`): stable per thread, `Join`/`Detach` return `EINVAL`, never reference-counted. This replaces the earlier "host main thread has no guest handle" behaviour, which gave the guest a null `scePthreadSelf()`.
- `scePthreadMutexattrSetprotocol` accepts `PRIO_NONE` (0), `PRIO_INHERIT` (1) and `PRIO_PROTECT` (2): guest priorities are recorded, not applied, so there is nothing to lend and no ceiling to apply, and such a mutex behaves like `PRIO_NONE`. Values outside 0..2 return `EINVAL`. (Upstream AnyPS5 70bbd700 accepted INHERIT only; FreeBSD and shadPS4 accept the whole range.)
- `sceKernelDeleteSema` (mutex-and-condition handle object until M4) wakes every waiter with `EACCES`, then blocks until the last waiter has left before freeing the object.

**Shared kernel error codes.** `libkernel/KernelErrors.hpp` defines every SCE kernel error as `0x80020000 | errno`. The equeue previously used an unrelated `0x8001xxxx` range (`ETIMEDOUT` was `0x80010023`), so a guest comparing `sceKernelWaitEqueue` against `SCE_KERNEL_ERROR_ETIMEDOUT` (`0x8002003C`) saw a different code than a console returns. Equeue, VideoOut and the semaphore now use the shared names. `DirectMemory.hpp` now includes the same header (it used to declare its own `0x8001xxxx` statics; see Open questions 6).

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
- **POSIX `usleep`:** `usleep_nid_postfix` is an alias of `sceKernelUsleep` that always returns 0, as FreeBSD does without signals (`Time/Time.cpp:149-152`).

**Error policy.**

- Real POSIX and SCE conditions return codes: null pointer → `EINVAL`, plus `EBUSY`, `ETIMEDOUT`, `EDEADLK`, `EPERM`, `EAGAIN`, `ESRCH`.
- A state that is truly unimplemented calls `[[noreturn]] Unsupported(const char* what)`. It logs the NID, caller offset and thread name, then calls `abort()`. It replaces the `throw` in `NotImplemented_nid_no_patch`, which the shared unwinder lets a guest `catch(...)` swallow.
- Host exceptions never cross an `APS5_VABI` boundary. Target: every export body is `noexcept`, enforced by `APS5_EXPORT_FN`. Today the macro exists but has no call sites: `libSceAgc` registers through the raw `APS5_EXPORT`, and `libSceSysmodule/Export.cpp` still throws (see [TechnicalDebt.md](../TechnicalDebt.md) "Host exceptions").

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
  - Upstream ports (PR #28, adapted to the repo's per-API error families): equeue wait/delete + error-check mutex (`GuestKernelErrors.cpp`), cond timedwait slices (`GuestCondTimedwait.cpp`), thread identity/lifecycle (`GuestThreadSelf.cpp`), host TLS balance incl. Win32 threads (`HostThreadLocal.cpp` + helper TU).
  - Kernel-sync ports: `PthreadSemHandleTests.cpp` (scePthreadSem return codes, lifecycle, token conservation), `PthreadBarrierOnceTests.cpp` (barrier rounds, serial thread, next-round regression, EBUSY destroy, `pthread_equal`, `sched_yield`, `scePthreadOnce`/`pthread_once`), `KernelSleepSignalTests.cpp` (`sceKernelSleep`, POSIX `usleep` returning 0 in `KernelSleep.UsleepSleepsAndReturnsZero`, `sigprocmask` incl. invalid `how`), `GuestKernelErrors.cpp` literal equeue codes, `PthreadSemTests.cpp` `sceKernelDeleteSema`, `GuestThreadSelf.cpp` adopted handles.
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
- **Microbenchmarks** (M1 exit): an uncontended lock/unlock pair is **at least 10× faster** than main@e06dbff's implementation. Also measured: contended hand-off latency, condition-variable round trip, `sceKernelUsleep(100)` error (p50/p99), and the CPU cost of a 1 ms sleep.
- **Local regression:** per-run telemetry counts contended waits, `Unsupported()` hits (target 0) and watchdog heartbeats. Bugsnax job-system coverage is checked at M4.

## Milestones

| Milestone | Delivers |
|---|---|
| M1 | - [x] The futex rewrite of mutex, condition variable, rwlock and `_umtx_op`, with no global mutex. Correct errno and SCE returns. `Unsupported()` in place of throws. Compact tids. Removal of the job-affinity and time-scale switches. The sync microbenchmark. (ROADMAP M1) |
| M4 | - [ ] Event flags, semaphores and fibers on the shared primitive, for UE4 job-system coverage. (ROADMAP M4) |
| M5 | - [ ] The perf pass over contention and sleep telemetry. (ROADMAP M5) |

## Open questions

1. Does any gate-title code read the mutex or condition slot as a pointer, for example `if (*m == NULL)`? The M1 NID inventory and a guarded trial run decide whether pointer mode is ever needed.
2. Is `_umtx_op` imported by any gate title directly, or only through our libkernel? This affects priority, not the design.
3. Does FIFO order matter for anything beyond FIFO-attributed semaphores? For example, would broadcast requeue avoid a thundering herd in job systems?
4. Should the guest priority bands map to Windows priorities at all, or would that starve the presenter and driver threads?
5. Sync assessment (PR #28): upstream `TimedWait` and `Pthread/Posix/Common.hpp` were trial-ported and reverted (unwired, no roadmap item names them; they return with the feature that needs them). Landed and green: GuestKernelErrors, GuestCondTimedwait, GuestThreadSelf, HostThreadLocal GTest suites — error expectations follow the repo's per-API families (equeue FreeBSD-style, pthread/SCE `0x8002`, cond `kSceTimedOut`), the host main thread was given no guest handle (superseded: it now gets an adopted handle, see Target design), and the Win32 TLS baseline is filter-proof. Reverted as unusable here: `tests/Fiber.cpp` (no fiber implementation; gates M4), `tests/GuestLocale.cpp` (bakes upstream's locale layout; ours differs), `tests/GuestDirectoryEntries.cpp` (pread/getdents are Unsupported stubs here).
6. Resolved (bean `portps5-mn0m`): `DirectMemory.hpp` includes `KernelErrors.hpp` and `tests/kernel/KernelErrorValuesTests.cpp` pins the literal values. The File module still computes `0x80020000 | errno` through its own `SceKernelErrno`/`HostErrnoToSce` (`File/include/File.hpp:15-18`, `File/src/Open.cpp:118-131`) instead of `KernelErrors.hpp`; the values agree, and the migration belongs to the filesystem owner. The original finding was that the kernel-sync port (AnyPS5 97cae145, 20810712) left `DirectMemory.hpp` on private `0x8001xxxx` statics (`EINVAL` was `0x80010005`, not `0x80020016`) that the tests in `tests/memory/` pinned; a header that declares its own statics cannot be included next to `KernelErrors.hpp`, so the header and its tests were migrated together.
7. `sceKernelAio*` (AnyPS5 c25c543f) is not ported: it needs a positional fd read/write primitive. `File/src/Stdio.cpp` `NativePread` emulates `pread` with seek, read and seek-restore, which races a concurrent reader on the same fd, and upstream's Aio used `_dup`, which shares the file offset on Windows. Port it on top of the filesystem owner's pread once that primitive is race-free.
8. Errno validation (PR #61 follow-up) against FreeBSD `lib/libc/gen/sem_new.c` and `lib/libthr/thread/{thr_barrier,thr_mutexattr}.c` (BSD-2-Clause), the Open POSIX Test Suite assertions for `pthread_barrier_*`/`sem_*`/`pthread_once`/`pthread_mutexattr_setprotocol` (legacy code GPLv2+), and Wine `kernelbase/tests/sync.c` (LGPL): `sem_trywait` empty is `EAGAIN`, `sem_init` value above `INT_MAX` is `EINVAL`, `sem_post` at the maximum is `EOVERFLOW`, `setprotocol` accepts 0..2, barrier count 0 is `EINVAL`, `destroy` with waiters is `EBUSY`, the last arrival is the serial thread and the barrier resets for reuse, `WaitOnAddress` only returns on wake/timeout/mismatch (all loops re-check). Deliberate difference: FreeBSD `sem_destroy` does not report `EBUSY` for sleeping waiters (POSIX leaves it undefined); we return `EBUSY` rather than free under a sleeper. `flag != 0` rejection on `scePthreadSemInit` rests on shadPS4/SharpEmu only. FIFO wake order for equal-priority waiters (Open POSIX sem_post 8) is not implemented.
9. Host core placement: the guest affinity masks name console cores and stay recorded but not applied (Target design, "Affinity"). Should the runtime steer threads by guest priority band instead, for example keeping the presenter, driver and the highest band off efficiency cores or a second CCD? Measure 1 % low on the reference tier with and without placement before deciding, in the M5 performance pass. No mask may be tuned to one title.
