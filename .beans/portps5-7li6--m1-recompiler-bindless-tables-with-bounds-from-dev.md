---
# portps5-7li6
title: 'M1: Recompiler bindless tables with bounds from device limits'
status: todo
type: feature
priority: normal
created_at: 2026-09-30T23:52:29Z
updated_at: 2026-09-30T23:52:29Z
---

## Context

Material scans are capped by fixed constants inherited from upstream (MaterialScanLimit, BindlessSlots, core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp). The ROADMAP M1 item asks for bindless tables with bounds taken from device limits, without a per-title cap. Spec: docs/spec/shader-recompiler.md Milestones M1, open question 4.

## Higher Goal

A general mechanism that removes title-tuned caps.

## Acceptance Criteria

- [ ] Bounds derived from VkPhysicalDeviceLimits and descriptor-indexing features
- [ ] Synthetic golden for a table larger than the old caps
- [ ] shader-recompiler.md M1 row and ROADMAP M1 item ticked

## Out of Scope

GPU-side descriptor heap (M4).

## Summary of Changes

TBD
