---
# portps5-maub
title: 'CI: install official Doxygen release with pinned checksum'
status: in-progress
type: task
created_at: 2026-10-01T00:26:27Z
updated_at: 2026-10-01T00:26:27Z
---

## Context
doxygen.yml used choco doxygen.install 1.13.2, which downloads from SourceForge and intermittently 404s, failing the gate on infrastructure.

## Higher Goal
Deterministic, reliable docs gate on the latest stable Doxygen.

## Acceptance Criteria
- [x] Install official 1.18.0 zip from doxygen.nl, SHA-256 pinned in env, retries, cache
- [x] doxygen docs/Doxyfile exits 0 with zero warnings on main (WARN_AS_ERROR unchanged)
- [x] Specs updated
- [ ] Hosted Doxygen gate passes on the PR

## Out of Scope
Re-enabling WARN_IF_UNDOCUMENTED / WARN_NO_PARAMDOC.

## Summary of Changes
.github/workflows/doxygen.yml, docs/spec/build-toolchain.md, docs/spec/verification.md.
