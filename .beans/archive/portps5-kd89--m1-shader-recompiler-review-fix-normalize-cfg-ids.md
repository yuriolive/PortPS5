---
# portps5-kd89
title: 'M1: Shader Recompiler review fix: normalize CFG IDs for divergent block and merge block lookup'
status: completed
type: task
priority: high
tags:
    - beads:portps5-nzg
created_at: 2026-09-30T22:53:53Z
updated_at: 2026-09-30T22:53:53Z
---

## Description

Context: Addressing macroscopeapp bot review comment on PR #29.
Higher Goal: Correctly map CFG block IDs from BlockInfo to IrBlock pointers to avoid mismatch between physical IrBlock::Id() and CFG IDs (which are shifted due to the synthetic entry block).
Acceptance Criteria:
- [x] SharedMemoryBarrierInserter maps BlockInfo.id to IrBlock* via program.BlockOrder() and program.Metadata().blockInfo
- [x] Divergent block check looks up block by CFG ID
- [x] Merge block lookup targets correct IrBlock* by CFG mergeId
- [x] Regression test DivergentBlockLdsWritePlacesBarrierAtReconvergence updated with shifted CFG IDs and passes
Summary of Changes:
- Modified SharedMemoryBarrierInserter.cpp, RecompilerFixesTests.cpp

Migrated from beads `portps5-nzg`.
