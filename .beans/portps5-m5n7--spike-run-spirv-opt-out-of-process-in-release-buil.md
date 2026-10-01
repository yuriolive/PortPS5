---
# portps5-m5n7
title: 'Spike: run spirv-opt out of process in release builds (PRD R1)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:23:43Z
updated_at: 2026-10-01T18:33:01Z
parent: portps5-7fqk
---

## Context

Release builds ship without the SPIRV-Tools optimizer because static linking is a GPL-2.0 conflict (PRD R1). Blocked by: none.

## Higher Goal

Know whether release shaders can use spirv-opt legally and cheaply.

## Acceptance Criteria

- [ ] Prototype spirv-opt as a separate process invoked by the recompiler; measure compile-time cost
- [ ] Legal note: separate program boundary for an Apache-2.0 tool
- [ ] Decision recorded in PRD R1 and shader-recompiler.md

## Out of Scope

Native optimizer passes (portps5-t5qa).

## Summary of Changes

TBD
