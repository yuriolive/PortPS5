---
# portps5-4une
title: 'libc: C11 and Dinkumware thread runtime on futex words'
status: todo
type: feature
created_at: 2026-10-01T21:42:53Z
updated_at: 2026-10-01T21:42:53Z
parent: portps5-dbpx
---

## Context

Imported but missing, with no reference implementation: `_Mtx_init`, `_Mtx_destroy`, `_Mtx_lock`, `_Mtx_unlock`, `_Cnd_init`, `_Cnd_destroy`, `_Cnd_wait`, `_Cnd_broadcast`, `_Thrd_detach`, `_Xtime_get_ticks`, and the `std::_Pad` launcher (`_ZNSt4_PadC2Ev`, `_ZNSt4_PadD2Ev`, `_ZNSt4_Pad7_LaunchEPP7pthread`, `_ZNSt4_Pad8_ReleaseEv`) that `std::thread` uses.

Source: NID gap analysis of a local, non-gate dump (PPSA04489), matched against built PortPS5 prx export tables at main@5dd65fe3 and the reference trees AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43 and shadPS4 fecfbed0. Only NIDs and function names were read from the dump. 'Imported' does not mean 'called': confirm against the gate-title inventories (portps5-zadg, portps5-3eh1) before porting. Licences: AnyPS5 and KytyPS5 GPL-2.0, shadPS4 GPL-2.0-or-later (port with the file's copyright header and cite the commit); SharpEmu is C#, so it is a behavioural reference only.

Blocked by: none.

## Higher Goal

Guest `std::mutex`, `std::condition_variable` and `std::thread` work, with the same futex-word rules as the rest of the runtime (threading.md).

## Acceptance Criteria

- [ ] `_Mtx_*` and `_Cnd_*` built on the in-place futex words, not heap-allocated std:: mutexes
- [ ] `_Pad` launches through scePthreadCreate with the guest TLS set-up
- [ ] GoogleTest: lock, contention, condition wait and broadcast, thread launch and detach
- [ ] threading.md lists the runtime

## Out of Scope

`std::jthread` and C++20 stop tokens.
