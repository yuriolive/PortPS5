---
# portps5-dktm
title: Per-pipeline shader statistics (register count, spills)
status: todo
type: task
created_at: 2026-10-01T21:35:20Z
updated_at: 2026-10-01T21:35:20Z
parent: portps5-etxc
---

## Context

VK_KHR_pipeline_executable_properties reports per-pipeline VGPR and SGPR counts, spills and instruction counts on drivers that support it. Recompiler output quality is otherwise only visible as GPU time.

Blocked by: none.

## Higher Goal

Shader-quality regressions from recompiler passes show up as numbers before they show up as fps.

## Acceptance Criteria

- [ ] `debug.recompiler.profile` also records executable statistics per pipeline when available
- [ ] A local report lists the top pipelines by register count and spills (hashes only)
- [ ] Optional numeric `shader_stats` totals in local results, not part of any pass rule

## Out of Scope

Vendor-specific counters.
