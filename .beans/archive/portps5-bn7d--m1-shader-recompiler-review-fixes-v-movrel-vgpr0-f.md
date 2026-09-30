---
# portps5-bn7d
title: 'M1: Shader Recompiler review fixes: v_movrel VGPR0 fallback and divergent LDS barriers'
status: completed
type: task
priority: high
tags:
    - beads:portps5-51j
created_at: 2026-09-30T22:53:50Z
updated_at: 2026-09-30T22:53:50Z
---

## Description

Context: Addressing macroscopeapp bot review comments on PR #29.
Higher Goal: Correct VGPR[0] fallback for v_movrels/v_movreld and avoid GPU deadlocks from workgroup barriers in divergent control flow.
Acceptance Criteria:
- [x] v_movrels falls back to reading VGPR[0] when M0 >= maxReg
- [x] v_movreld writes to VGPR[0] when M0 >= maxReg
- [x] SharedMemoryBarrierInserter places barriers at reconvergence merge points instead of inside divergent blocks
- [x] SharedAtomicIAdd32 with zero addend preserved for workgroup synchronization
- [x] Regression tests added to RecompilerFixesTests and pass
- [x] CI expected count updated to 141
Out of Scope: Full Function-storage array VGPR lowering (M3).
Summary of Changes:
- Modified ControlFlowInstructions.cpp, SharedMemoryBarrierInserter.cpp, RecompilerFixesTests.cpp, tests/expected-count

Migrated from beads `portps5-51j`.
