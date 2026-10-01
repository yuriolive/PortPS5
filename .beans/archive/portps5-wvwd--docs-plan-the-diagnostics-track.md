---
# portps5-wvwd
title: 'Docs: plan the diagnostics track'
status: completed
type: task
created_at: 2026-10-01T21:36:27Z
updated_at: 2026-10-01T21:36:27Z
---

## Context

Telemetry and profiling were planned (performance track), but failure triage was not: device loss logs only packet history, no validation layer runs, captures are anonymous, crash reports carry raw addresses, and the asan preset cannot link with MinGW GCC 15.2. Blocked by: none.

## Higher Goal

Make every local failure diagnosable from local artifacts, planned with dependencies so the work can run in parallel.

## Acceptance Criteria

- [x] Epic portps5-etxc with nine child beans, parents and blockers recorded
- [x] portps5-jy7n gains present-to-display latency and jitter
- [x] gpu-driver.md Diagnostics subsection and failure-mode row; configuration.md planned debug keys; verification.md CI validation and crash-report items; build-toolchain.md asan row corrected
- [x] ROADMAP Diagnostics track with deliverables and a parallel-lanes table

## Out of Scope

Implementing any diagnostic. Changing the shipped compiler.

## Summary of Changes

.beans/ (epic portps5-etxc and children portps5-636k, ey3w, bbxe, c5if, 7e2a, cmyp, dktm, h2rv, hmw8; jy7n amended), docs/ROADMAP.md, docs/spec/gpu-driver.md, configuration.md, verification.md, build-toolchain.md.
