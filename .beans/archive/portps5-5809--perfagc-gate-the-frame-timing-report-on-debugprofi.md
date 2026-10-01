---
# portps5-5809
title: 'perf(agc): gate the frame timing report on debug.profile gpu'
status: completed
type: task
priority: normal
created_at: 2026-10-01T16:40:21Z
updated_at: 2026-10-01T16:40:21Z
---

## Context
`FrameTiming::Print` formatted ~20 KB of `[FrameTiming]` text per frame and flushed stdout on every flip, which dominates frame time on a console. AnyPS5 `94c443e` gates the print; PortPS5 had no gate. Review also found the typed config was never loaded at startup, so a `debug.profile` gate could not be enabled (startup wiring: bean portps5-ftci).

## Higher Goal
Frame-time instrumentation is a profiling aid enabled by the typed `[debug]` config, not default output (M2 perf bar).

## Acceptance Criteria
- [x] Report written only when `[debug] profile` contains `gpu`; collection unchanged.
- [x] Submission-lineage invariant still checked with the report off (death test).
- [x] Verified on a converted gate title: default prints 0 `[FrameTiming]` lines, `profile = ["gpu"]` prints them.
- [x] Tests: `agc_frame_timing_gate_tests`, `ConfigStartup.*` (9/9).
- [ ] Console before/after fps on the reference tier (open: needs a maintainer console A/B).

## Out of Scope
Per-draw PerformanceTimer cost; AnyPS5 draw recipes.

## Summary of Changes
`PerformanceTimer.hpp` (gate + lineage order), `tests/agc/FrameTimingGateTests.cpp`, `tests/config/ConfigStartupTests.cpp`, `docs/spec/configuration.md`. PR #71.
