---
# portps5-f9a3
title: 'M1: Runtime telemetry (frame-time log, watchdog, structured logs)'
status: completed
type: feature
priority: high
created_at: 2026-09-30T23:49:46Z
updated_at: 2026-10-01T17:59:26Z
parent: portps5-r8mh
---


## Context

No frame-time logging, softlock/crash watchdog or structured run log exists in core/ on main; only audio underrun and overrun counters exist per context. The verification protocol (docs/spec/verification.md sections 3 and 4) and PRD F9 depend on these, including audio underrun and latency counters and the A/V offset skeleton (video_latency_ms, docs/spec/video-fmv.md).

## Higher Goal

Every regression and full run produces the telemetry the results JSON needs, with no game data in the logs (ROADMAP M1 runtime telemetry).

## Acceptance Criteria

- [x] Frame-time log (dt per present; the reader excludes gaps over 1 s as stalls)
- [x] Watchdog (core, abort path) flags a softlock after 30 s without present or guest thread progress and calls a per-queue diagnostics hook (hook registration only; no queue registers one yet)
- [x] Structured events (audio.underrun, video_latency_ms, av.offset, generic numeric Event); always on, no config key
- [x] Unit tests on synthetic clocks (`telemetry_core_tests`)
- [x] verification.md and ROADMAP M1 telemetry item updated

## Out of Scope

tools/regress upload script, results JSON schema changes.

## Summary of Changes

Landed in PR #78: telemetry core, runtime exports, watchdog, mixer counters, telemetry_core_tests and telemetry_runtime_tests, spec verification.md 4.3. The call-site wiring (Start at process start-up, presenter NotePresent/SetVideoLatencyMs, guest-thread NoteGuestProgress) is new work tracked in portps5-w1re.
