---
# portps5-6625
title: 'docs: performance track and steady-state invariants'
status: completed
type: task
priority: normal
created_at: 2026-10-01T18:03:34Z
updated_at: 2026-10-01T18:10:18Z
---

## Context

Per-draw performance work was landing (PRs #71, #74, #75, #77) while the ROADMAP had only a one-line "Performance pass" in M5, and the PRD bar (30 fps) said nothing about designs that would block 60 fps or the planned v2 open-world tier. Beans carried no dependency links, so nobody could see which work could run in parallel. Blocked by: none. Stacked on PR #89 (status resync).

## Higher Goal

The architecture keeps a path to 60 fps and to v2 without a rewrite, every optimization is measured, and the bean graph shows what can be worked on in parallel.

## Acceptance Criteria

- [x] PRD §4.5 steady-state invariants P1–P6 with owner specs and rules; §5 non-goal line points at them
- [x] ROADMAP "Performance track" P0–P3 with beans, a Parallel lanes table and a traceability row
- [x] verification.md §4.4 planned fields: frame percentiles, baseline compare, frame breakdown, violation counters
- [x] gpu-driver.md Decision: invariants with the known violations, and a GPU IR with resource states and a queue tag for the M3 split; open question 11 (multithreaded recording)
- [x] threading.md open question 9 (host core placement)
- [x] AGENTS.md: record `--blocked-by` and `--parent` on every bean, and keep Parallel lanes tables
- [x] Beans: epic portps5-7fqk with children portps5-hfiw, portps5-aifo, portps5-rrll; blocked_by edges w1re→c06p, dtwf→c06p, r7qk→w1re, hfiw→w1re+tiod, aifo→w1re

## Out of Scope

v2 goals, the v2 roadmap (M7–M10), the host platform layer spec and Linux (next PR). Implementing any P0 item.

## Summary of Changes

docs/PRD.md, docs/ROADMAP.md, docs/spec/verification.md, docs/spec/gpu-driver.md, docs/spec/threading.md, AGENTS.md; new beans 7fqk, hfiw, aifo, rrll; dependency edges on w1re, dtwf, r7qk.
