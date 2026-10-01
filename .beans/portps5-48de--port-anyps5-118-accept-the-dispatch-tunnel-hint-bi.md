---
# portps5-48de
title: 'Port AnyPS5 #118: accept the dispatch tunnel hint bit'
status: todo
type: task
priority: normal
created_at: 2026-10-01T20:53:15Z
updated_at: 2026-10-01T20:53:15Z
parent: portps5-4ut1
---

## Context

AnyPS5 d1bf55c (#118, 5 lines) accepts the dispatch tunnel hint bit; PortPS5 has no handling. Upstream source: AnyPS5 main@709d7fe (pulled 2026-10-01); cite the source commit in the port's commit body (.agents/rules/git-workflow.md). Blocked by: none.

## Higher Goal

Dispatches carrying the hint are not rejected.

## Acceptance Criteria

- [ ] Hint bit accepted and documented as inert
- [ ] PM4 decode test

## Out of Scope

Tunnel semantics.

## Summary of Changes

TBD
