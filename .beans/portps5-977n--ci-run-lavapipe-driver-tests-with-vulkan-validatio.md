---
# portps5-977n
title: 'CI: run lavapipe driver tests with Vulkan validation layers, fail on errors'
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:01:19Z
updated_at: 2026-10-01T19:01:20Z
parent: portps5-7n6b
---

## Context

driver_lavapipe runs without VK_LAYER_KHRONOS_validation, so invalid usage passes silently. Blocked by: none.

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] Pinned, SHA-256-verified Khronos validation layer installed in the lavapipe job
- [ ] Tests run with the layer enabled through a test hook or loader setting (no APS5_ env var)
- [ ] A validation error fails the job; known false positives listed with reasons
- [ ] verification.md section 1 updated

## Out of Scope

Release builds with layers.

## Summary of Changes

TBD
