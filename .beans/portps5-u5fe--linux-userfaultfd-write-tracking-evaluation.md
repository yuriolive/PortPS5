---
# portps5-u5fe
title: Evaluate a Linux userfaultfd backend for write tracking
status: todo
type: task
priority: low
created_at: 2026-10-01T03:30:00Z
updated_at: 2026-10-01T17:59:27Z
parent: portps5-w3s8
---


## Context

AnyPS5 d8c7c0cc tracks guest writes on Linux with an async-write-protect userfaultfd and `PAGEMAP_SCAN` (`libc/src/GuestWriteWatch.cpp`, about 330 lines) but is wired into upstream's AGC driver, adds four `APS5_*` environment switches and is motivated by a title. `WriteWatchTracker` here is built on Win32 `GetWriteWatch` and is not instantiated by the runtime yet (bean portps5-421p). Reviewed and deferred in docs/spec/guest-memory.md.

## Higher Goal

A Linux test and CI backend for the real IWriteTracker, if it can sit behind a platform seam.

## Acceptance Criteria

- [ ] `WriteWatchTracker` gets a platform seam (Win32 `GetWriteWatch` and a Linux provider) after portps5-421p
- [ ] Only the libc-level `GuestWriteWatch` is considered; the AGC driver stays untouched
- [ ] Capabilities probed at startup, reported through typed `[debug]` config, no env vars
- [ ] GoogleTest on this kernel and a documented skip where the kernel lacks the feature

## Out of Scope

Any change to the AGC driver or the Recorder submit path.

## Summary of Changes

TBD
