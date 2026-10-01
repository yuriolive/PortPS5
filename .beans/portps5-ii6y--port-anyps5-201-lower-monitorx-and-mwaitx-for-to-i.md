---
# portps5-ii6y
title: 'Port AnyPS5 #201: lower MONITORX and MWAITX for --to-intel'
status: todo
type: feature
created_at: 2026-10-01T21:08:49Z
updated_at: 2026-10-01T21:08:49Z
---

## Context

PortPS5 recognises MONITORX/MWAITX but marks them Unsupported (relinker.md 'MONITORX family Unsupported'); AnyPS5 f694c13 (#201) lowers them (e.g. to a pause/yield sequence). Spin-wait loops in engines use them. Upstream: AnyPS5 main@709d7fe; cite the source commit in the port's commit body. Blocked by: none.

## Higher Goal

Spin-wait code runs on Intel hosts instead of aborting.

## Acceptance Criteria

- [ ] Lowering ported; semantics documented (wait is a hint, a yield is correct)
- [ ] Relinker test on synthetic sites
- [ ] relinker.md updated

## Out of Scope

Power-state accuracy.

## Summary of Changes

TBD
