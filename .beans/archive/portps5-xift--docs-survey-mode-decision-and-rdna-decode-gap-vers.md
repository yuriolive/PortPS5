---
# portps5-xift
title: 'docs: survey-mode decision and RDNA decode gap versus reference projects'
status: completed
type: task
created_at: 2026-10-01T21:19:36Z
updated_at: 2026-10-01T21:19:36Z
---

## Context

Abort-on-first-failure finds one gap per run. Nothing measured RDNA decode coverage against other PS5 projects. Blocked by: none.

## Higher Goal

Find missing pieces quickly, without weakening the pass rule, and close shader gaps that independent decoders agree on.

## Acceptance Criteria

- [x] Survey-mode decision in docs/spec/README.md, configuration.md (`debug.survey_unsupported`) and verification.md; implementation bean portps5-huyy
- [x] tools/compare_isa.py with tests; coverage table in shader-recompiler.md
- [x] One bean per instruction group: gzgk, a2u2, dpay, naoi, p22g, j0i5, kc2b, 3fi7

## Out of Scope

Implementing survey mode or any instruction.

## Summary of Changes

docs/spec/README.md, configuration.md, verification.md, shader-recompiler.md; tools/compare_isa.py; tests/tools/test_compare_isa.py; 9 new beans.
