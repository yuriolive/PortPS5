---
# portps5-3fi7
title: 'Recompiler: decode and translate S_MEMREALTIME'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:19:02Z
updated_at: 2026-10-01T21:19:02Z
parent: portps5-oo21
---

## Context

tools/compare_isa.py (2026-10-01; AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43) first listed five instructions in this group. The four S_CBRANCH_CDBG* branches were a counting error: the decoder maps all four to RdnaOpcode::SCbranchCdbg (RdnaScalarOpDecoder.cpp, SOPP 0x17-0x1a) and the translator and tests cover them, but tools/progress.py had no one-to-many alias. The alias is fixed, which leaves S_MEMREALTIME (KytyPS5). Use the RDNA2 ISA for semantics. KytyPS5 is GPL-2.0, so its code may be read, and copied if its header is kept. Blocked by: none.

## Higher Goal

Close decode and translation gaps that independent decoders agree exist, starting with the agreed ones.

## Acceptance Criteria

- [ ] Decode and translate S_MEMREALTIME with a synthetic golden shader (no game bytecode)
- [ ] spirv-val clean; recompiler fuzz corpus green
- [ ] tools/progress.py decoded and translated counts rise accordingly
- [ ] shader-recompiler.md coverage table refreshed

## Out of Scope

Instructions not in this group.

## Summary of Changes

TBD
