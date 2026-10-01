---
# portps5-kc2b
title: 'Recompiler: decode and translate DS 64-bit atomics, inc/dec, cmpst, mskor'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:19:01Z
updated_at: 2026-10-01T21:19:02Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) lists 8 RDNA2 instructions in this group that a reference decodes and PortPS5 does not; 4 are decoded by two or more references. Names and holders: DS_ADD_U64 (KytyPS5/SharpEmu), DS_CMPST_B32 (SharpEmu), DS_CMPST_RTN_B32 (SharpEmu), DS_DEC_U32 (KytyPS5/SharpEmu), DS_INC_U32 (KytyPS5/SharpEmu), DS_MSKOR_B32 (SharpEmu), DS_OR_B64 (KytyPS5/SharpEmu), DS_WRITE_B8_D16_HI (KytyPS5). Use the RDNA2 ISA for semantics; reference code may be read for ideas (SharpEmu GPL-2.0-or-later, KytyPS5 and AnyPS5 GPL-2.0 compatible; check before copying). Blocked by: none.

## Higher Goal

Close decode and translation gaps that independent decoders agree exist, starting with the agreed ones.

## Acceptance Criteria

- [ ] Decode and translate the agreed names first, then the rest, each with a synthetic golden shader (no game bytecode)
- [ ] spirv-val clean; recompiler fuzz corpus green
- [ ] tools/progress.py decoded and translated counts rise accordingly
- [ ] shader-recompiler.md coverage table refreshed

## Out of Scope

Instructions not in this group.

## Summary of Changes

TBD
