---
# portps5-naoi
title: 'Recompiler: decode and translate D16 buffer and global loads/stores and missing buffer/global atomics'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:19:00Z
updated_at: 2026-10-01T21:19:00Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) lists 20 RDNA2 instructions in this group that a reference decodes and PortPS5 does not; 0 are decoded by two or more references. Names and holders: BUFFER_ATOMIC_AND_X2 (KytyPS5), BUFFER_ATOMIC_DEC (SharpEmu), BUFFER_ATOMIC_INC (SharpEmu), BUFFER_LOAD_SBYTE_D16 (SharpEmu), BUFFER_LOAD_SBYTE_D16_HI (SharpEmu), BUFFER_LOAD_SHORT_D16 (SharpEmu), BUFFER_LOAD_SHORT_D16_HI (SharpEmu), BUFFER_LOAD_UBYTE_D16 (SharpEmu), BUFFER_LOAD_UBYTE_D16_HI (SharpEmu), BUFFER_STORE_BYTE_D16_HI (SharpEmu), BUFFER_STORE_SHORT_D16_HI (SharpEmu), GLOBAL_ATOMIC_ADD (SharpEmu), GLOBAL_LOAD_SBYTE_D16 (SharpEmu), GLOBAL_LOAD_SBYTE_D16_HI (SharpEmu), GLOBAL_LOAD_SHORT_D16 (SharpEmu), GLOBAL_LOAD_SHORT_D16_HI (SharpEmu), GLOBAL_LOAD_UBYTE_D16 (SharpEmu), GLOBAL_LOAD_UBYTE_D16_HI (SharpEmu), GLOBAL_STORE_BYTE_D16_HI (SharpEmu), GLOBAL_STORE_SHORT_D16_HI (SharpEmu). Use the RDNA2 ISA for semantics; reference code may be read for ideas (SharpEmu GPL-2.0-or-later, KytyPS5 and AnyPS5 GPL-2.0 compatible; check before copying). Blocked by: none.

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
