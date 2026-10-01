---
# portps5-37j0
title: 'Host platform layer: core/host interfaces, Win32 backend, contract tests, Win32 allowlist'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:12:41Z
updated_at: 2026-10-01T18:15:26Z
parent: portps5-epoi
---

## Context

Win32 calls sit at their call sites (WaitOnAddress in 11 files, GetWriteWatch in libc WriteTracker, VEH in one file; docs/spec/host-platform.md Current state). Blocked by: none.

## Higher Goal

Native Linux in 2.0 (M7) is a new backend, not a rewrite; new 1.0 code has one place to call the OS.

## Acceptance Criteria

- [ ] core/host/ interfaces for futex, clock and virtual memory per host-platform.md Target design, returning codes, never throwing
- [ ] Win32 backend; the futex core in libkernel/Pthread moved onto it as the first consumer, behaviour unchanged
- [ ] Contract GoogleTest suites (portps5_add_gtest) written against the interface, not the backend
- [ ] policy job: allowlist of files outside core/host/ that include windows.h or call Win32; fails when the list grows
- [ ] host-platform.md checkboxes and ROADMAP seam row ticked

## Out of Scope

Linux backend (M7). Migrating every existing Win32 call site (they move when touched). Threads, faults, files and module loading interfaces (follow-up beans).

## Summary of Changes

TBD
