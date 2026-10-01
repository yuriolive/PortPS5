---
# portps5-rw2y
title: 'libc: scanf %Nc host divergence on Windows'
status: todo
type: bug
priority: low
created_at: 2026-09-30T23:51:29Z
updated_at: 2026-10-01T18:33:27Z
parent: portps5-7dk3
---


## Context

The Windows host scanf accepts a short %Nc field: sscanf(buf, "%8c%8c") on a shorter input returns 2 where C and FreeBSD return 1. Fixing it needs an own scanf engine. Recorded in docs/spec/libc.md Open questions.

## Higher Goal

Guest scanf matches FreeBSD behaviour on every host.

## Acceptance Criteria

- [ ] Own scanf engine, or a pre-scan that enforces field width for %c
- [ ] Regression test comparing against the FreeBSD result
- [ ] libc.md Open question closed

## Out of Scope

Other scanf extensions.

## Summary of Changes

TBD
