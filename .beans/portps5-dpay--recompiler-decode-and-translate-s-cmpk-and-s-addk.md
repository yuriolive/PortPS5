---
# portps5-dpay
title: 'Recompiler: decode and translate S_CMPK_* and S_ADDK_I32'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:19:00Z
updated_at: 2026-10-01T21:19:00Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) lists 13 RDNA2 instructions in this group that a reference decodes and PortPS5 does not; 0 are decoded by two or more references. Names and holders: S_ADDK_I32 (SharpEmu), S_CMPK_EQ_I32 (SharpEmu), S_CMPK_EQ_U32 (SharpEmu), S_CMPK_GE_I32 (SharpEmu), S_CMPK_GE_U32 (SharpEmu), S_CMPK_GT_I32 (SharpEmu), S_CMPK_GT_U32 (SharpEmu), S_CMPK_LE_I32 (SharpEmu), S_CMPK_LE_U32 (SharpEmu), S_CMPK_LG_I32 (SharpEmu), S_CMPK_LG_U32 (SharpEmu), S_CMPK_LT_I32 (SharpEmu), S_CMPK_LT_U32 (SharpEmu). Use the RDNA2 ISA for semantics; reference code may be read for ideas (SharpEmu GPL-2.0-or-later, KytyPS5 and AnyPS5 GPL-2.0 compatible; check before copying). Blocked by: none.

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
