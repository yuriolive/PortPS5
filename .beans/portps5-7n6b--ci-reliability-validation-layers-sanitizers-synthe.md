---
# portps5-7n6b
title: 'CI reliability: validation layers, sanitizers, synthetic guests, fuzzing, codegen metrics'
status: todo
type: epic
created_at: 2026-10-01T19:01:18Z
updated_at: 2026-10-01T19:01:18Z
---

## Context

Hosted CI has GoogleTest, the golden SPIR-V corpus and lavapipe, but no Vulkan validation layers, no sanitizers, no end-to-end guest run, no PE structure checks, no fuzzing and no codegen metrics (verification.md 1.1). Blocked by: none; the children carry their blockers.

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] All child beans completed or scrapped with reasons

## Out of Scope

Game-data tests (local only).

## Summary of Changes

TBD
