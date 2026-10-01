---
# portps5-xl4k
title: 'CI: Fix Doxygen 1.13.2 install (SourceForge 404)'
status: todo
type: bug
priority: high
created_at: 2026-09-30T23:50:10Z
updated_at: 2026-09-30T23:50:10Z
---

## Context

The Doxygen API Doc Gate (.github/workflows/doxygen.yml, step Install Doxygen 1.13.2 via choco install doxygen.install --version=1.13.2) can fail for an infrastructure reason: the package downloads from SourceForge, which returns 404 for that version. The gate is a required check, so unrelated PRs are blocked. Official downloads are at https://www.doxygen.nl/download.html and in the doxygen GitHub releases.

## Higher Goal

A deterministic, pinned Doxygen install that does not depend on a dead mirror; real documentation errors remain the only failure cause.

## Acceptance Criteria

- [ ] Install step fetches the pinned Doxygen from an official release with a verified checksum, or pins a choco version whose source still resolves
- [ ] Gate passes on main with the Doxyfile unchanged
- [ ] Comment in doxygen.yml and docs/spec/build-toolchain.md doxygen-doc-gate entry name the new source and version

## Out of Scope

Re-enabling WARN_IF_UNDOCUMENTED or WARN_NO_PARAMDOC.

## Summary of Changes

TBD
