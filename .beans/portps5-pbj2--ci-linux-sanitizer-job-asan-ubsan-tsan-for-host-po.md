---
# portps5-pbj2
title: 'CI: Linux sanitizer job (ASan, UBSan, TSan) for host-portable test targets'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T19:01:20Z
updated_at: 2026-10-01T19:01:20Z
parent: portps5-7n6b
blocked_by:
    - portps5-37j0
---

## Context

MinGW ships no sanitizer runtimes and links statically, so the asan preset is local and Linux only. Host-portable targets (recompiler, relinker, extent allocator, telemetry core, config, futex core behind core/host) can run on ubuntu-latest. The Recorder segfault (portps5-3maf) is the kind of bug TSan finds. Blocked by: portps5-37j0 (host platform layer, so sync and memory code builds on Linux).

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] ubuntu-latest job builds the host-portable targets with GCC and -fsanitize=address,undefined; a separate TSan run for the sync and Recorder logic
- [ ] Targets that need Win32 are excluded by label, not by skipping assertions
- [ ] Job is required once green

## Out of Scope

Sanitizing the Windows prx build.

## Summary of Changes

TBD
