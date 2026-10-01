---
# portps5-zieb
title: 'libkernel: implement stubbed TSD-key, clock, file and memory exports'
status: todo
type: feature
created_at: 2026-10-01T21:28:53Z
updated_at: 2026-10-01T21:28:53Z
parent: portps5-7dk3
---

## Context

Verified on PortPS5 main@5dd65fe3: `scePthreadKeyCreate` (libkernel/Pthread/src/Tsd.cpp) and `sceKernelClockGettime` (libkernel/Time/Time.cpp) are NotImplemented stubs. The feature-gap review also lists `pthread_key_*`, `sceKernelGettimeofday`, `sceKernelReadTsc`, `sceKernelGetTscFrequency`, `sceKernelFstat`, `sceKernelPread`, `sceKernelPwrite`, `sceKernelGetdents`, `sceKernelRename`, `sceKernelMtypeprotect`, `sceKernelQueryMemoryProtection` and `sceKernelLoadStartModule`; each needs re-verifying (`sceKernelBatchMap` and `sceKernelDlsym` turned out implemented). AnyPS5 implements most of them.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Gate titles get through libkernel start-up without hitting the abort path in a common export.

## Acceptance Criteria

- [ ] Each listed export re-verified on main; already-implemented ones dropped from this bean
- [ ] The rest implemented with SCE/POSIX return codes per threading.md and guest-memory.md
- [ ] GoogleTest per export: success path, error codes, edge cases
- [ ] Import inventory (portps5-zadg) shows none of them reached by Dreaming Sarah or TMNT as a stub

## Out of Scope

Fibers and event flags (portps5-k34n). AMPR (separate bean).
