---
# portps5-vfk7
title: 'Kernel: POSIX sem_* stubs'
status: todo
type: task
priority: normal
created_at: 2026-09-30T23:51:23Z
updated_at: 2026-10-01T18:33:26Z
parent: portps5-7dk3
---



## Context

Posix/Sem.cpp (core/libs/prx/libkernel/Pthread/Posix/Sem.cpp) still routes sem_init, sem_destroy, sem_wait and the rest through NotImplemented_nid_no_patch. PR #61 (merged) ported the scePthread semaphore lifecycle on futex words but explicitly left these POSIX names out of scope. Spec: docs/spec/threading.md.

## Higher Goal

POSIX semaphores share the futex-word primitive with the SCE ones and return POSIX codes.

## Acceptance Criteria

- [ ] sem_* exports implemented over the shared futex semaphore
- [ ] Return-code tests (EAGAIN on trywait of empty, EINVAL on bad init, EOVERFLOW at INT_MAX)
- [ ] Reuses the futex semaphore from PR #61 (landed); `libkernel/Semaphore` and `Pthread/src/Sem.cpp` are the references

## Out of Scope

sceKernelCancelSema.

## Summary of Changes

TBD
