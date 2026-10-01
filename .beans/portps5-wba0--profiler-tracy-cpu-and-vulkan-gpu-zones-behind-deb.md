---
# portps5-wba0
title: 'Profiler: Tracy CPU and Vulkan GPU zones behind [debug] profile'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:25:11Z
updated_at: 2026-10-01T18:33:01Z
parent: portps5-7fqk
---

## Context

Telemetry gives per-frame totals; finding which host function is slow needs a profiler. Blocked by: none.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) hotspots found by measurement, not guesswork.

## Acceptance Criteria

- [ ] Tracy (BSD-3, pinned submodule) behind [debug] profile, compiled out by default
- [ ] CPU zones in HLE hot paths and driver; Vulkan GPU zones
- [ ] Zero cost when off (verified by microbenchmark)
- [ ] configuration.md [debug] profile documents the value

## Out of Scope

Shipping Tracy in release builds.

## Summary of Changes

TBD
