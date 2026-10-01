---
# portps5-t33t
title: 'Port AnyPS5 #209: lower CLZERO for --to-intel'
status: todo
type: feature
created_at: 2026-10-01T21:08:49Z
updated_at: 2026-10-01T21:08:49Z
---

## Context

PortPS5 recognises CLZERO but does not lower it; AnyPS5 131156c (#209, ~450 lines) lowers it to an equivalent cache-line zeroing sequence. Upstream: AnyPS5 main@709d7fe; cite the source commit in the port's commit body. Blocked by: none.

## Higher Goal

Memory-clearing code using CLZERO runs on Intel hosts.

## Acceptance Criteria

- [ ] Lowering ported with a test that the 64-byte aligned line is zeroed
- [ ] relinker.md updated

## Out of Scope

Performance tuning of the replacement.

## Summary of Changes

TBD
