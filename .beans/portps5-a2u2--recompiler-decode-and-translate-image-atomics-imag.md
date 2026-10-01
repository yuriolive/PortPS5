---
# portps5-a2u2
title: 'Recompiler: decode and translate image atomics, IMAGE_GATHER4 variants and other MIMG forms (BVH ray ops belong to portps5-jehk)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:18:59Z
updated_at: 2026-10-01T21:18:59Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) lists 50 RDNA2 instructions in this group that a reference decodes and PortPS5 does not; 4 are decoded by two or more references. Names and holders: IMAGE_ATOMIC_CMPSWAP (SharpEmu), IMAGE_ATOMIC_DEC (SharpEmu), IMAGE_ATOMIC_FCMPSWAP (SharpEmu), IMAGE_ATOMIC_FMAX (KytyPS5/SharpEmu), IMAGE_ATOMIC_FMIN (KytyPS5/SharpEmu), IMAGE_ATOMIC_INC (SharpEmu), IMAGE_ATOMIC_SMAX (KytyPS5/SharpEmu), IMAGE_ATOMIC_SMIN (KytyPS5/SharpEmu), IMAGE_ATOMIC_SUB (SharpEmu), IMAGE_GATHER4 (SharpEmu), IMAGE_GATHER4_C_B_CL (SharpEmu), IMAGE_GATHER4_L (KytyPS5), IMAGE_SAMPLE_B (SharpEmu), IMAGE_SAMPLE_B_CL (SharpEmu), IMAGE_SAMPLE_B_CL_O (SharpEmu), IMAGE_SAMPLE_B_O (SharpEmu), IMAGE_SAMPLE_C (SharpEmu), IMAGE_SAMPLE_CD (SharpEmu), IMAGE_SAMPLE_CD_CL (SharpEmu), IMAGE_SAMPLE_CD_CL_O (SharpEmu), IMAGE_SAMPLE_CD_O (SharpEmu), IMAGE_SAMPLE_CL (SharpEmu), IMAGE_SAMPLE_CL_O (SharpEmu), IMAGE_SAMPLE_C_B (SharpEmu), IMAGE_SAMPLE_C_B_CL (SharpEmu), IMAGE_SAMPLE_C_B_CL_O (SharpEmu), IMAGE_SAMPLE_C_B_O (SharpEmu), IMAGE_SAMPLE_C_CD (SharpEmu), IMAGE_SAMPLE_C_CD_CL (SharpEmu), IMAGE_SAMPLE_C_CD_CL_O (SharpEmu), IMAGE_SAMPLE_C_CD_O (SharpEmu), IMAGE_SAMPLE_C_CL (SharpEmu), IMAGE_SAMPLE_C_CL_O (SharpEmu), IMAGE_SAMPLE_C_D (SharpEmu), IMAGE_SAMPLE_C_D_CL (SharpEmu), IMAGE_SAMPLE_C_D_CL_O (SharpEmu), IMAGE_SAMPLE_C_D_O (SharpEmu), IMAGE_SAMPLE_C_L (SharpEmu), IMAGE_SAMPLE_C_LZ (SharpEmu), IMAGE_SAMPLE_C_LZ_O (SharpEmu), IMAGE_SAMPLE_C_L_O (SharpEmu), IMAGE_SAMPLE_C_O (SharpEmu), IMAGE_SAMPLE_D (SharpEmu), IMAGE_SAMPLE_D_CL (SharpEmu), IMAGE_SAMPLE_D_CL_O (SharpEmu), IMAGE_SAMPLE_D_O (SharpEmu), IMAGE_SAMPLE_L (SharpEmu), IMAGE_SAMPLE_LZ_O (SharpEmu), IMAGE_SAMPLE_L_O (SharpEmu), IMAGE_SAMPLE_O (SharpEmu). Use the RDNA2 ISA for semantics; reference code may be read for ideas (SharpEmu GPL-2.0-or-later, KytyPS5 and AnyPS5 GPL-2.0 compatible; check before copying). Blocked by: none.

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
