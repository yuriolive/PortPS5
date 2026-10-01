---
# portps5-hx0t
title: 'Time: sleep without busy-waiting'
status: todo
type: task
created_at: 2026-10-01T21:28:55Z
updated_at: 2026-10-01T21:28:55Z
parent: portps5-7dk3
---

## Context

SleepNanos (libkernel/Time/Time.cpp:77-95, PortPS5 main@5dd65fe3) sleeps in whole milliseconds, then spins on YieldProcessor for the last 1-2 ms; any sleep under 2 ms spins for its full length. KytyPS5 6f24b03 removes the equivalent busy wait.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Guest sleeps cost no CPU while keeping the accuracy threading.md requires.

## Acceptance Criteria

- [ ] High-resolution waitable timer (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) for the tail instead of spinning
- [ ] Microbenchmark: p50 and p99 sleep error and CPU time, before and after

## Out of Scope

Changing the global timer resolution.
