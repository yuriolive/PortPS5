---
# portps5-lak5
title: 'M1: Finish kernel sync ports onto the futex design (PR #61)'
status: completed
type: task
priority: normal
created_at: 2026-09-30T23:47:02Z
updated_at: 2026-10-01T00:20:58Z
---


## Context

PR #61 (merged 2026-09-30 as be11124e) ported vetted AnyPS5 kernel-sync commits (pthread semaphores, barriers, once, sleep, sigprocmask, DeleteSema, priority-protocol mutexes, scePthreadSelf) onto the futex words in docs/spec/threading.md. The branch was rebased, its Semaphore.hpp Doxygen issue fixed (`2caa0447`) and merged.

## Higher Goal

Fill real libkernel gaps without heap std::mutex/condvar in guest sync and without throws crossing APS5_VABI.

## Acceptance Criteria

- [x] Branch rebased on main with merge conflicts resolved
- [x] Doxygen errors in core/libs/prx/libkernel/Semaphore/include/Semaphore.hpp fixed if they reproduce on the current head
- [x] All ported exports have return-code tests; ctest -L unit green locally
- [x] threading.md Tests and Open questions 6 and 7 merged with main's text
- [x] PR merged

## Out of Scope

Aio lifecycle, sceKernelLseek, DirectMemory error constants, POSIX sem_* stubs (separate beans), sceKernelCancelSema.

## Summary of Changes

PR #61 merged (be11124e): futex-based `scePthreadSem*`, `pthread_barrier_*`, `scePthreadOnce`/`pthread_once`, `sceKernelSleep`, `sigprocmask`, `sceKernelDeleteSema`, priority-protocol mutex attributes, adopted `scePthreadSelf` handles, shared `KernelErrors.hpp`; tests `PthreadSemHandleTests`, `PthreadBarrierOnceTests`; `docs/spec/threading.md` Target design, Tests and Open questions 6 to 8. Still open from that PR's scope notes: portps5-vfk7 (POSIX sem_*), portps5-j4e1 (Aio), portps5-mn0m (DirectMemory error constants), portps5-65h0 (Lseek).
