---
# portps5-d0l3
title: 'libkernel: POSIX socket helpers, select and pthread_cond_destroy'
status: todo
type: task
created_at: 2026-10-01T21:42:54Z
updated_at: 2026-10-01T21:42:54Z
parent: portps5-7dk3
---

## Context

Stubbed: `inet_ntop`, `inet_pton` (`libkernel/Socket/src/Socket.cpp:68-78`; AnyPS5 `Socket.cpp:11-15`), `select` (`Socket.cpp:87`; KytyPS5 `libKernel.cpp:2235`). Missing: `pthread_cond_destroy` (AnyPS5 `Pthread/Posix/Cond.cpp:22`). `sceKernelGettimeofday` and `sceKernelGetdirentries` are in portps5-zieb. `__tls_get_addr` shows up as missing but needs no export: the relinker special-cases the TLS resolver (relinker.md).

Source: NID gap analysis of a local, non-gate dump (PPSA04489), matched against built PortPS5 prx export tables at main@5dd65fe3 and the reference trees AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43 and shadPS4 fecfbed0. Only NIDs and function names were read from the dump. 'Imported' does not mean 'called': confirm against the gate-title inventories (portps5-zadg, portps5-3eh1) before porting. Licences: AnyPS5 and KytyPS5 GPL-2.0, shadPS4 GPL-2.0-or-later (port with the file's copyright header and cite the commit); SharpEmu is C#, so it is a behavioural reference only.

Blocked by: none.

## Higher Goal

The POSIX layer that middleware uses offline returns real results.

## Acceptance Criteria

- [ ] `inet_ntop` and `inet_pton` for IPv4 and IPv6 with POSIX error codes
- [ ] `select` over the socket layer, with a timeout and EBADF for unknown descriptors
- [ ] `pthread_cond_destroy` on the futex condition word
- [ ] GoogleTest per function

## Out of Scope

Online networking.
