---
# portps5-gzgk
title: 'Recompiler: decode and translate V_CMPX_* compare-and-write-exec variants (16/64-bit and F/class forms)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:18:59Z
updated_at: 2026-10-01T21:18:59Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) lists 64 RDNA2 instructions in this group that a reference decodes and PortPS5 does not; 20 are decoded by two or more references. Names and holders: V_CMPX_EQ_I16 (AnyPS5/KytyPS5/SharpEmu), V_CMPX_EQ_I64 (SharpEmu), V_CMPX_EQ_U16 (AnyPS5/KytyPS5/SharpEmu), V_CMPX_EQ_U64 (SharpEmu), V_CMPX_F_F16 (SharpEmu), V_CMPX_F_F32 (SharpEmu), V_CMPX_F_I32 (SharpEmu), V_CMPX_F_I64 (SharpEmu), V_CMPX_F_U32 (SharpEmu), V_CMPX_F_U64 (SharpEmu), V_CMPX_GE_F64 (KytyPS5), V_CMPX_GE_I16 (AnyPS5/KytyPS5/SharpEmu), V_CMPX_GE_I64 (SharpEmu), V_CMPX_GE_U16 (AnyPS5/SharpEmu), V_CMPX_GE_U64 (SharpEmu), V_CMPX_GT_I16 (AnyPS5/KytyPS5/SharpEmu), V_CMPX_GT_I64 (SharpEmu), V_CMPX_GT_U64 (SharpEmu), V_CMPX_LE_F64 (KytyPS5), V_CMPX_LE_I16 (AnyPS5/KytyPS5/SharpEmu), V_CMPX_LE_I64 (SharpEmu), V_CMPX_LE_U16 (AnyPS5/SharpEmu), V_CMPX_LE_U64 (KytyPS5/SharpEmu), V_CMPX_LG_F16 (SharpEmu), V_CMPX_LT_I16 (AnyPS5/KytyPS5/SharpEmu), V_CMPX_LT_I64 (SharpEmu), V_CMPX_LT_U64 (SharpEmu), V_CMPX_NE_I16 (AnyPS5/KytyPS5/SharpEmu), V_CMPX_NE_U16 (AnyPS5/SharpEmu), V_CMPX_NGE_F16 (SharpEmu), V_CMPX_NLE_F16 (KytyPS5/SharpEmu), V_CMPX_NLG_F16 (SharpEmu), V_CMPX_O_F16 (SharpEmu), V_CMPX_O_F32 (KytyPS5/SharpEmu), V_CMPX_TRU_F16 (SharpEmu), V_CMPX_TRU_F32 (SharpEmu), V_CMPX_T_I32 (SharpEmu), V_CMPX_T_I64 (SharpEmu), V_CMPX_T_U32 (SharpEmu), V_CMPX_T_U64 (SharpEmu), V_CMPX_U_F16 (SharpEmu), V_CMPX_U_F32 (SharpEmu), V_CMP_EQ_F64 (KytyPS5), V_CMP_F_F16 (SharpEmu), V_CMP_F_I64 (SharpEmu), V_CMP_F_U64 (SharpEmu), V_CMP_GE_I64 (SharpEmu), V_CMP_GE_U64 (KytyPS5/SharpEmu), V_CMP_GT_I64 (SharpEmu), V_CMP_LE_F64 (KytyPS5), V_CMP_LE_I64 (KytyPS5/SharpEmu), V_CMP_LE_U64 (KytyPS5/SharpEmu), V_CMP_LT_I64 (KytyPS5/SharpEmu), V_CMP_NE_I64 (SharpEmu), V_CMP_NGE_F16 (KytyPS5/SharpEmu), V_CMP_NGT_F16 (KytyPS5/SharpEmu), V_CMP_NLE_F16 (SharpEmu), V_CMP_NLG_F16 (SharpEmu), V_CMP_NLT_F16 (KytyPS5/SharpEmu), V_CMP_O_F16 (SharpEmu), V_CMP_TRU_F16 (SharpEmu), V_CMP_T_I64 (SharpEmu), V_CMP_T_U64 (SharpEmu), V_CMP_U_F16 (SharpEmu). Use the RDNA2 ISA for semantics; reference code may be read for ideas (SharpEmu GPL-2.0-or-later, KytyPS5 and AnyPS5 GPL-2.0 compatible; check before copying). Blocked by: none.

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
