---
# portps5-idoi
title: 'Port AnyPS5 #200: lower SHA-256 instructions for --to-intel'
status: todo
type: feature
created_at: 2026-10-01T21:08:49Z
updated_at: 2026-10-01T21:08:49Z
---

## Context

AnyPS5 2612729 (#200, ~790 lines) lowers SHA-NI instructions (sha256rnds2, sha256msg1/2) for Intel hosts without them. PortPS5's relinker has no SHA handling. Upstream: AnyPS5 main@709d7fe; cite the source commit in the port's commit body. Blocked by: none.

## Higher Goal

Titles using SHA-NI run on Intel hosts after --to-intel.

## Acceptance Criteria

- [ ] Lowering ported with a differential test against a reference SHA-256
- [ ] Residual report counts SHA sites
- [ ] relinker.md updated

## Out of Scope

Hosts that have SHA-NI (lowering only when needed).

## Summary of Changes

TBD
