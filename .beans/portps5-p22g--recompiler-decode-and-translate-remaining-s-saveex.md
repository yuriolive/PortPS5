---
# portps5-p22g
title: 'Recompiler: decode and translate remaining S_*_SAVEEXEC forms, S_SWAPPC_B64, S_BFE_I64'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:19:01Z
updated_at: 2026-10-01T21:19:01Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) lists 10 RDNA2 instructions in this group that a reference decodes and PortPS5 does not; 0 are decoded by two or more references. Names and holders: S_NAND_SAVEEXEC_B32 (SharpEmu), S_NAND_SAVEEXEC_B64 (SharpEmu), S_NOR_SAVEEXEC_B32 (SharpEmu), S_NOR_SAVEEXEC_B64 (SharpEmu), S_ORN1_SAVEEXEC_B32 (SharpEmu), S_ORN1_SAVEEXEC_B64 (SharpEmu), S_SWAPPC_B64 (SharpEmu), S_XNOR_SAVEEXEC_B32 (SharpEmu), S_XNOR_SAVEEXEC_B64 (SharpEmu), S_BFE_I64 (SharpEmu). Use the RDNA2 ISA for semantics; reference code may be read for ideas (SharpEmu GPL-2.0-or-later, KytyPS5 and AnyPS5 GPL-2.0 compatible; check before copying). Blocked by: none.

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
