---
# portps5-ibu3
title: 'docs: CI reliability plan (validation layers, sanitizers, synthetic guests, fuzzing)'
status: completed
type: task
priority: normal
created_at: 2026-10-01T19:02:28Z
updated_at: 2026-10-01T19:02:28Z
---

## Context

The CI/CD practices other PS3/PS4/PS5 projects use were compared against PortPS5's CI. The comparison found these missing: validation layers, sanitizers, an end-to-end synthetic guest run, PE structure checks, codegen metrics, fuzzing, clang-tidy, C++ coverage, a locked perf environment, a local nightly run, and Unsupported() counts. Blocked by: none. Stacked on PR #92.

## Higher Goal

Catch regressions on hosted CI without a GPU or game data, and keep game-run checks local.

## Acceptance Criteria

- [x] Epic portps5-7n6b with 10 child beans and blocked_by edges
- [x] verification.md section 1.1 and the ROADMAP CI reliability lanes match the bean links
- [x] Perf-scene and compat-list beans extended (locked environment, status tiers)

## Out of Scope

Implementing any check. Self-hosted runners (not allowed on the public repo).

## Summary of Changes

docs/spec/verification.md section 1.1, docs/ROADMAP.md CI reliability section, new beans 7n6b, 977n, pbj2, ktrt, s9zz, g0n7, axx6, hpx7, 02a4, e6xg, ba7d; portps5-52bs and portps5-4bkt criteria extended.
