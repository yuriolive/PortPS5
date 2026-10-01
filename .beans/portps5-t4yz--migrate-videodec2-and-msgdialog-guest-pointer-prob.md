---
# portps5-t4yz
title: Migrate Videodec2 and MsgDialog guest-pointer probes to GuestMemoryValidation
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:31:09Z
updated_at: 2026-10-01T18:33:22Z
parent: portps5-w3s8
---

## Context

libSceVideodec2 (#88) uses the stopgap core/libs/GuestRangeCheck.hpp (VirtualQuery, ignores the guest-memory registry) and MsgDialog has its own probe; the shared GuestMemoryValidation API landed in PR #83 (bean portps5-8l0d). Blocked by: none.

## Higher Goal

One guest-pointer validation path for all PRXs (docs/spec/guest-memory.md Validation API).

## Acceptance Criteria

- [ ] Both modules call GuestMemoryValidation; GuestRangeCheck.hpp removed
- [ ] Existing pointer-probe tests still pass; add a registry-aware case

## Out of Scope

Other PRXs.

## Summary of Changes

TBD
