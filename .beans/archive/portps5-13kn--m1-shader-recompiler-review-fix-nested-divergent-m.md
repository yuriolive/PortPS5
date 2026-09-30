---
# portps5-13kn
title: 'M1: Shader Recompiler review fix: nested divergent merge mapping and indirect branch target propagation'
status: completed
type: task
priority: high
tags:
    - beads:portps5-27j
created_at: 2026-09-30T22:53:49Z
updated_at: 2026-09-30T22:53:49Z
---

## Description

Context: Addressing macroscopeapp bot review comments #4127047689 and #4127047712 on PR #29.
Higher Goal: Ensure LDS writes in nested divergent control flow are synchronized at the innermost reconvergence point, and indirect-branch targets reached from divergent regions are properly identified as divergent to avoid GPU control barrier deadlocks.
Acceptance Criteria:
- [x] SharedMemoryBarrierInserter maps nested divergent blocks to their innermost merge block
- [x] SharedMemoryBarrierInserter enqueues indirectTargets during divergent region traversal
- [x] Regression test NestedDivergentBlockLdsWritePlacesBarrierAtInnermostMerge passes
- [x] Regression test IndirectBranchTargetInDivergentRegionIsClassifiedAsDivergent passes
- [x] Expected test count updated to 155
Summary of Changes:
- Modified SharedMemoryBarrierInserter.cpp, RecompilerFixesTests.cpp, tests/expected-count

Migrated from beads `portps5-27j`.
