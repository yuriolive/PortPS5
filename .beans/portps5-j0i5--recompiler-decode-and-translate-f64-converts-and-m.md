---
# portps5-j0i5
title: 'Recompiler: decode and translate f64 converts and math (VOP1/VOP3), 16-bit min3/max3/med3 and other VOP3/VOP2/VOP3P gaps'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:19:01Z
updated_at: 2026-10-01T21:19:01Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) lists 30 RDNA2 instructions in this group that a reference decodes and PortPS5 does not; 7 are decoded by two or more references. Names and holders: V_CVT_F32_F64 (KytyPS5/SharpEmu), V_CVT_F64_F32 (KytyPS5), V_CVT_F64_I32 (KytyPS5/SharpEmu), V_CVT_F64_U32 (KytyPS5/SharpEmu), V_MOVRELSD_2_B32 (SharpEmu), V_MOVRELSD_B32 (SharpEmu), V_RCP_F64 (KytyPS5/SharpEmu), V_RSQ_F64 (SharpEmu), V_SQRT_F64 (SharpEmu), V_ADD_CO_U32 (SharpEmu), V_ADD_F64 (KytyPS5), V_FMA_F64 (KytyPS5/SharpEmu), V_MAD_I16 (KytyPS5), V_MAD_U32_U16 (SharpEmu), V_MAX3_I16 (AnyPS5), V_MAX3_U16 (AnyPS5), V_MED3_U16 (AnyPS5), V_MIN3_I16 (AnyPS5), V_MIN3_U16 (AnyPS5), V_MUL_F64 (KytyPS5/SharpEmu), V_MUL_LO_U16 (KytyPS5), V_SAD_HI_U8 (SharpEmu), V_SAD_U16 (SharpEmu), V_SAD_U8 (SharpEmu), V_SUBREV_CO_U32 (SharpEmu), V_SUB_CO_U32 (SharpEmu), V_FMAC_F32 (SharpEmu), V_MUL_HI_I32_I24 (AnyPS5), V_MUL_HI_U32_U24 (AnyPS5/SharpEmu), V_FMA_MIX_F32 (SharpEmu). Use the RDNA2 ISA for semantics; reference code may be read for ideas (SharpEmu GPL-2.0-or-later, KytyPS5 and AnyPS5 GPL-2.0 compatible; check before copying). Blocked by: none.

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
