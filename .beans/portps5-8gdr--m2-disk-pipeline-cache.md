---
# portps5-8gdr
title: 'M2: Disk pipeline cache'
status: todo
type: feature
priority: high
created_at: 2026-09-30T23:53:10Z
updated_at: 2026-10-01T18:33:21Z
parent: portps5-dbpx
---



## Context

No persistent shader or pipeline cache exists; every run recompiles. Spec: docs/spec/pipeline-cache.md (prerequisites: request serialisation and agc_shader_replay are ported, the recompiler-golden corpus exists).

## Higher Goal

PRD F7: with a warm cache telemetry logs 0 shader compilations and 0 pipeline creations during a regression pass.

## Acceptance Criteria

- [ ] SourceKey/VariantKey as defined in pipeline-cache.md
- [ ] EnvKey also hashes the enabled Vulkan features and extensions, so adding v2 stages (ray tracing, mesh) never needs a cache format break
- [ ] All five cache files with warm-up and telemetry counters
- [ ] Lavapipe and golden tests for key stability and corruption handling
- [ ] pipeline-cache.md M2 row and ROADMAP M2 item ticked

## Out of Scope

Bounded hash-indexed variants (M3).

## Summary of Changes

TBD
