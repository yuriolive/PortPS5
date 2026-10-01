---
# portps5-2cfq
title: 'libkernel: APR/AMPR asynchronous file I/O'
status: todo
type: feature
created_at: 2026-10-01T21:28:53Z
updated_at: 2026-10-01T21:28:53Z
parent: portps5-bxvu
blocked_by:
    - portps5-j4e1
---

## Context

Only `sceKernelAddAmprEvent` exists on PortPS5 main@5dd65fe3 (libkernel/Equeue/Equeue.cpp). KytyPS5 src/libs/libAmpr.cpp, SharpEmu SharpEmu.Libs/Ampr/ and AnyPS5 libkernel/Apr/src/Apr.cpp implement the command-buffer API; AnyPS5 added it for the Demon's Souls boot.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Streaming titles read through AMPR command buffers instead of aborting.

## Acceptance Criteria

- [ ] Command-buffer build, submit and wait with SCE return codes
- [ ] Built on the Aio request lifecycle from portps5-j4e1
- [ ] GoogleTest on synthetic command buffers and files

## Out of Scope

Hardware decompression (2.0, M8).
