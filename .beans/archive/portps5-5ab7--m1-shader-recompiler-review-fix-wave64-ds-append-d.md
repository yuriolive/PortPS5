---
# portps5-5ab7
title: 'M1: Shader Recompiler review fix: wave64 ds_append / ds_consume LDS barrier insertion'
status: completed
type: task
priority: high
tags:
    - beads:portps5-bhm
created_at: 2026-09-30T22:53:52Z
updated_at: 2026-09-30T22:53:52Z
---

## Description

Context: Addressing macroscopeapp bot review comment on PR #29.
Higher Goal: Ensure ds_append and ds_consume operations in wave64 compute programs have workgroup LDS barriers so subsequent reads observe shared memory updates.
Acceptance Criteria:
- [x] SharedMemoryBarrierInserter includes SharedAccess::Append and SharedAccess::Consume alongside Write and Atomic
- [x] Regression test SharedMemoryBarrierInsertedAfterDataAppendAndConsumeWave64 passes
- [x] Expected test count updated to 153
Summary of Changes:
- Modified SharedMemoryBarrierInserter.cpp, RecompilerFixesTests.cpp, tests/expected-count

Migrated from beads `portps5-bhm`.
