---
# portps5-3maf
title: Fix lavapipe SEGFAULT in RecorderTest.SubmitMakesDeviceWritesVisibleToTheHost on main
status: todo
type: bug
priority: critical
created_at: 2026-10-01T18:31:09Z
updated_at: 2026-10-01T18:33:22Z
parent: portps5-4ut1
---

## Context

The driver_lavapipe job on main (run 36905103154, commit 1c65844a) fails: RecorderTest.SubmitMakesDeviceWritesVisibleToTheHost (agc_recorder_tests) segfaults. The run at 07b76f75 passed. Blocked by: none.

## Higher Goal

Recorder submit-path tests are reliable before the Recorder is wired into sceAgcDriverSubmitDcb (portps5-tiod).

## Acceptance Criteria

- [ ] Root cause identified (regression from #68 or #88, or a flaky race), with the evidence in this bean
- [ ] Fix plus a test that fails without it; repeat-run the test (--gtest_repeat) to rule out flakiness
- [ ] driver_lavapipe green on main

## Out of Scope

Recorder wiring.

## Summary of Changes

TBD
