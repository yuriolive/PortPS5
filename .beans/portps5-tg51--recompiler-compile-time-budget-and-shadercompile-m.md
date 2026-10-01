---
# portps5-tg51
title: 'Recompiler: compile-time budget and shader.compile_ms telemetry'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:23:44Z
updated_at: 2026-10-01T18:32:57Z
parent: portps5-7fqk
blocked_by:
    - portps5-w1re
---

## Context

Cold-run stutter and warm-up time depend on recompile time, which telemetry doesn't report (shader-recompiler.md M5 row cites shader.compile_ms). Blocked by: portps5-w1re.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) warm-up time is measured and budgeted.

## Acceptance Criteria

- [ ] shader.compile_ms and pipeline.create_ms numeric events
- [ ] Results JSON compile time totals and p99
- [ ] Budget recorded per gate title in pipeline-cache.md

## Out of Scope

Compile-time optimizations themselves.

## Summary of Changes

TBD
