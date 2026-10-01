---
# portps5-3hk7
title: 'Pthread: accept adaptive mutex type 4'
status: todo
type: bug
created_at: 2026-10-01T21:28:54Z
updated_at: 2026-10-01T21:28:54Z
parent: portps5-rd6g
---

## Context

`scePthreadMutexattrSettype` accepts types 1-3 and returns EINVAL for 4 (libkernel/Pthread/src/Mutex.cpp:256-265, PortPS5 main@5dd65fe3). shadPS4 kernel/threads/mutex.cpp handles the adaptive type; SharpEmu 90c72eb notes it blocked UE titles.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

UE-based titles (Bugsnax) can create adaptive mutexes.

## Acceptance Criteria

- [ ] Type 4 accepted and mapped onto the futex mutex with documented semantics (spin-then-wait or Normal)
- [ ] GoogleTest: type 4 lock, unlock, trylock, and EINVAL for an unknown type
- [ ] threading.md mutex section updated

## Out of Scope

Priority-protocol changes.
