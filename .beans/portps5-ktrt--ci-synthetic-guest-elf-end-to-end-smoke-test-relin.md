---
# portps5-ktrt
title: 'CI: synthetic guest ELF end-to-end smoke test (relink, run, exit code)'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T19:01:21Z
updated_at: 2026-10-01T19:01:21Z
parent: portps5-7n6b
blocked_by:
    - portps5-sjuv
---

## Context

The relinker tests relink a synthetic ELF but never run it, so a broken loader or export table is found only with a game dump. A hand-assembled guest ELF (no Sony SDK, no game code) importing our own libkernel/libc NIDs can be relinked and run on the Windows runner. Blocked by: portps5-sjuv (cross-prx imports must resolve on main).

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] Generator for synthetic guest ELFs written for this project (hand-assembled, NIDs computed from public symbol names), checked into tests/
- [ ] Each case relinks, runs headless against the built prx libraries and asserts the exit code: thread create/join, mutex, file I/O in a sandbox, memory map/unmap, an Unsupported() abort path
- [ ] Registered in ctest (label unit or a new e2e label in the ci preset)
- [ ] Legal note in TESTING.md: no SDK headers, no game bytes

## Out of Scope

GPU work in the synthetic guest (lavapipe follow-up).

## Summary of Changes

TBD
