---
# portps5-i4kl
title: 'docs: v2 roadmap, host platform layer and v1 seams'
status: completed
type: task
priority: normal
created_at: 2026-10-01T18:12:43Z
updated_at: 2026-10-01T18:15:43Z
---

## Context

The maintainer set a 2.0 objective: an open-world AAA tier with GTA VI at 30 fps, on Windows and native Linux. The PRD and ROADMAP covered only 1.0 and listed Linux as a non-goal, with no plan for which 1.0 interfaces 2.0 depends on. Blocked by: none. Stacked on PR #90.

## Higher Goal

1.0 builds the seams 2.0 plugs into (design for 2.0, implement for 1.0), so no 1.0 subsystem has to be rewritten for 2.0, and every task's blockers are recorded so parallel work is visible.

## Acceptance Criteria

- [x] PRD §10: 2.0 objective, gates 6-9 (Horizon Forbidden West, Ratchet & Clank: Rift Apart, Marvel's Spider-Man 2, GTA VI; unpinned), V1-V6, the 2.0 tier rule, non-goals, risks V-R1 to V-R4; §5 Linux line points at §10 and the host platform layer
- [x] ROADMAP: Part I / Part II, the "v2 seams in v1" table with parallel lanes, M3 GPU IR reference, Part II M7-M11 with a dependency graph, 2.0 traceability
- [x] New spec docs/spec/host-platform.md with all nine sections; spec README index row
- [x] Beans: epic portps5-epoi with seams 37j0, qfac, gkef, hps3, l77s, jehk, j4e1 and spikes u16x, mdu8; M3 split portps5-hkwd under portps5-4ut1; blocked_by edges gkef→421p, r2ns→gkef, hkwd→tiod, hps3→hkwd, j4e1→37j0; portps5-8gdr gains the EnvKey feature-hash criterion

## Out of Scope

Implementing any seam or spike. Beans for M7-M11 (created by each milestone seed). Pinning the 2.0 gate titles.

## Summary of Changes

docs/PRD.md (§5 line, §10), docs/ROADMAP.md (Part I/II, seams, M7-M11, 2.0 traceability, M3 GPU IR line), docs/spec/host-platform.md (new), docs/spec/README.md (index row), 11 new beans, edges on r2ns, j4e1, 8gdr.
