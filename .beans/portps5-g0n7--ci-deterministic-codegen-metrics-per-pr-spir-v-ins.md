---
# portps5-g0n7
title: 'CI: deterministic codegen metrics per PR (SPIR-V instruction counts, relinker residual counts)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:01:22Z
updated_at: 2026-10-01T19:01:23Z
parent: portps5-7n6b
---

## Context

Shader-optimizer and relinker changes can regress output quality with every test still green. Instruction counts are deterministic, so hosted CI can track them. Blocked by: none.

## Higher Goal

Reliability: catch regressions on hosted CI without a GPU or game data (docs/spec/verification.md section 1.1).

## Acceptance Criteria

- [ ] A tool reports per-golden-shader SPIR-V instruction counts and the relinker stub/residual counts on the synthetic ELFs
- [ ] PR comment shows the delta against the base, like progress-report
- [ ] An increase over a threshold labels the PR for review; never a silent pass

## Out of Scope

Wall-clock benchmarks on hosted runners (noisy).

## Summary of Changes

TBD
