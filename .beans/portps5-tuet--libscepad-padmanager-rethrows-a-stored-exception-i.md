---
# portps5-tuet
title: 'libScePad: PadManager rethrows a stored exception inside noexcept exports (std::terminate)'
status: todo
type: bug
priority: normal
created_at: 2026-10-01T18:53:42Z
updated_at: 2026-10-01T18:53:43Z
parent: portps5-dbpx
---

## Context

PadManager rethrows a stored exception inside noexcept exports, which ends in std::terminate instead of an SCE error or the Unsupported() path (TechnicalDebt.md host exceptions). Blocked by: none.

## Higher Goal

No host exception reaches an APS5_VABI export (cpp-style.md Errors).

## Acceptance Criteria

- [ ] Exports return SCE error codes or abort through Unsupported() with a log
- [ ] Death or return-code GoogleTest for the failing path

## Out of Scope

Other modules' throws (TechnicalDebt.md table).

## Summary of Changes

TBD
