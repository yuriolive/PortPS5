---
# portps5-i58o
title: CPU microbenchmarks (Google Benchmark, test-only) with a nightly trend
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:25:11Z
updated_at: 2026-10-01T18:33:00Z
parent: portps5-7fqk
---

## Context

Hosted CI has no GPU and no game data, so host CPU regressions go unnoticed. Blocked by: none.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) regressions in hot host paths caught without a GPU.

## Acceptance Criteria

- [ ] Google Benchmark as a test-only dependency (Apache-2.0, never linked into shipped binaries; licence noted)
- [ ] Benchmarks: PM4 parse, texture compare/hash, recompile throughput, futex, allocator
- [ ] Nightly job records results as an artifact; trend only, never a PR gate

## Out of Scope

GPU benchmarks.

## Summary of Changes

TBD
