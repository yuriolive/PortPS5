---
# portps5-03bi
title: 'libc: measure guest allocator hot path; thread-local caches only if profiling shows a hotspot'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:24:57Z
updated_at: 2026-10-01T18:32:59Z
parent: portps5-7fqk
blocked_by:
    - portps5-wba0
---

## Context

Guest malloc goes through the libc mspace (docs/spec/libc.md). Whether it is a hotspot is unmeasured. Blocked by: portps5-wba0 (Tracy).

## Higher Goal

Allocator work happens only where profiling shows it matters.

## Acceptance Criteria

- [ ] Tracy zones on guest malloc/free/realloc paths; report share of frame CPU on two gate titles
- [ ] If above 2% of frame CPU: thread-local caches for small sizes with a differential test against the current allocator
- [ ] libc.md updated with the measurement

## Out of Scope

Replacing the mspace.

## Summary of Changes

TBD
