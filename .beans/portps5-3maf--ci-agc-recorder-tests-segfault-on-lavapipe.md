---
# portps5-3maf
title: 'CI: agc_recorder_tests segfault on lavapipe'
status: todo
type: bug
priority: critical
created_at: 2026-10-01T19:00:00Z
updated_at: 2026-10-01T19:26:15Z
parent: portps5-4ut1
---



## Context

The `driver_lavapipe` job fails on `main` at 1c65844a (run 36905103154): `RecorderTest.SubmitMakesDeviceWritesVisibleToTheHost` (agc_recorder_tests) dies with SEGFAULT after `[ RUN ]` and before `[ OK ]`; the run at 07b76f75 passed and the gdb rerun with `--gtest_repeat=150` passed. Run 36899104678 hit the same signature on three other tests of the same executable (`HostImportTest.UntrackedOrReadOnlyPagesFallBackToStaging`, `RecorderTest.SyncThroughIgnoresUnwrittenRanges`, `RecorderTest.SyncFromInsideACompletionDoesNotWaitForLaterBatches`), so the crash is not specific to one test body. Blocked by: none.

## Higher Goal

A deterministic driver_lavapipe job: a red lavapipe run means a real driver regression.

## Acceptance Criteria

- [ ] Crash dumps captured from CI (diagnostics landed in the first PR)
- [ ] Root cause identified and documented
- [ ] Fix in place, with a test that fails without it
- [ ] `ctest --preset lavapipe` passes locally on lavapipe, including under repeated parallel runs

## Out of Scope

Lavapipe performance; other driver suites.

## Investigation so far

- No Agc code changed between 07b76f75 (green) and 1c65844a: not a regression from #68/#88, a pre-existing flake.
- Local lavapipe (Mesa 24.3.4, same pin as CI): 1600 parallel runs of the failing test and 150 repeats of all Recorder/HostImport tests at -j12 (about 7350 processes), zero crashes.
- Lead (unconfirmed): fixture teardown (vkDestroyDevice/vkDestroyInstance/vulkan-1.dll unload with lavapipe threads alive); the crash is always after `[ RUN ]`, before `[ OK ]`, near the normal test duration.
- CI now writes WER LocalDumps minidumps for lavapipe crashes and uploads them with agc_recorder_tests.exe (`lavapipe-crashdumps` artifact) for offline symbolization.
- 2026-10-01, run 36928318025 (main@616bbd21): `RecorderTest.SyncThroughIgnoresUnwrittenRanges` segfaulted again, but the `lavapipe-crashdumps` artifact held only the exe: WER LocalDumps writes nothing on the hosted runner. GoogleTest's SEH guard turns a main-thread fault into a test failure, so a raw 0xC0000005 exit points at a fault on an unguarded thread (driver worker threads).
- `tests/CrashReporter.cpp` (linked into agc_recorder_tests) installs a last-chance unhandled-exception filter: prints code, fault address and the faulting thread's stack as module+RVA to stderr (visible in the ctest log), and writes a full minidump to `$CRASHDUMP_DIR`, which the existing upload step collects. Death test `CrashReporterDeathTest.WorkerThreadFaultIsReportedAndStillFatal` covers it (fails with the filter not installed).

## Summary of Changes

TBD
