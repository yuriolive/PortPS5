---
# portps5-c1oi
title: 'Pipeline compile off the submit thread: graphics pipeline library fast-link, background optimized relink, parallel warm-up'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:25:09Z
updated_at: 2026-10-01T18:33:00Z
parent: portps5-7fqk
blocked_by:
    - portps5-8gdr
---

## Context

Pipeline creation happens on the submit thread; skipping a draw while compiling is forbidden (no-title-hacks: never skip work). pipeline-cache.md open question 3. Blocked by: portps5-8gdr.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) invariant P3: no blocking compile on the submit thread after warm-up, even on a cold cache.

## Acceptance Criteria

- [ ] VK_EXT_graphics_pipeline_library fast-link from cached libraries at first use, optimized pipeline built in the background and swapped in
- [ ] Parallel warm-up pool
- [ ] No draw is skipped; a stall is counted (aifo) when fast-link is unavailable
- [ ] lavapipe tests; before/after on a cold run

## Out of Scope

Ahead-of-time compilation (post-2.0).

## Summary of Changes

TBD
