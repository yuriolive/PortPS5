---
# portps5-4foo
title: 'progress.py: count translated shader instructions next to decoded'
status: completed
type: task
created_at: 2026-10-01T21:05:36Z
updated_at: 2026-10-01T21:05:36Z
---

## Context

tools/progress.py counted a shader instruction as done when the decoder enum names it. Comparing with KytyPS5 and SharpEmu needs a second number: whether the translator handles it. Blocked by: none.

## Higher Goal

The progress report shows decode and translation coverage, so it compares with other projects' translators.

## Acceptance Criteria

- [x] shaders.translated, translated_percent, translated_names and per-group translated in progress.json; a line on stdout; a note on index.html
- [x] Only decoded entries count; aliases and FLAT_ twins handled like the decoded count; a missing Translation/ directory yields 0
- [x] Tests: TranslatedCountTests (two cases)
- [x] verification.md progress-report row updated

## Out of Scope

Badge and treemap changes; operand-form coverage.

## Summary of Changes

tools/progress.py, tests/tools/test_progress.py, docs/spec/verification.md. On main: decoded 591/1166, translated 590/1166.
