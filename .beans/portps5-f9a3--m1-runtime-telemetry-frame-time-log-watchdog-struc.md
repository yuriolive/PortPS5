---
# portps5-f9a3
title: 'M1: Runtime telemetry (frame-time log, watchdog, structured logs)'
status: todo
type: feature
priority: high
created_at: 2026-09-30T23:49:46Z
updated_at: 2026-09-30T23:49:46Z
---

## Context

No frame-time logging, softlock/crash watchdog or structured run log exists in core/ on main; only audio underrun and overrun counters exist per context. The verification protocol (docs/spec/verification.md sections 3 and 4) and PRD F9 depend on these, including audio underrun and latency counters and the A/V offset skeleton (video_latency_ms, docs/spec/video-fmv.md).

## Higher Goal

Every regression and full run produces the telemetry the results JSON needs, with no game data in the logs (ROADMAP M1 runtime telemetry).

## Acceptance Criteria

- [ ] Frame-time log with stall detection (gaps over 1 s between presents)
- [ ] Watchdog flags a softlock after 30 s without present or guest thread progress and dumps per-queue state
- [ ] Structured log events (dialog.open, audio.underrun, video_latency_ms) under the typed [debug]/telemetry config
- [ ] Unit tests on synthetic clocks
- [ ] verification.md and ROADMAP M1 telemetry item updated

## Out of Scope

tools/regress upload script, results JSON schema changes.

## Summary of Changes

TBD
