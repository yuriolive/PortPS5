---
# portps5-wg4m
title: 'Port AnyPS5 #195: import directory for Windows guest modules with TLS'
status: todo
type: bug
created_at: 2026-10-01T20:53:14Z
updated_at: 2026-10-01T20:53:14Z
---

## Context

AnyPS5 a7fdb49 (#195): a guest module with TLS and no import directory shares the executable's TLS block. Both Dreaming Sarah and the TMNT dump ship guest modules (libc). PortPS5's relinker has no equivalent (grep found none). Upstream source: AnyPS5 main@709d7fe (pulled 2026-10-01); cite the source commit in the port's commit body (.agents/rules/git-workflow.md). Blocked by: none.

## Higher Goal

Guest-module thread-locals are isolated from the executable's.

## Acceptance Criteria

- [ ] Verify the defect on a synthetic guest module with TLS
- [ ] Port the fix (83 lines upstream) with a relinker test
- [ ] relinker.md updated

## Out of Scope

Linux ELF output.

## Summary of Changes

TBD
