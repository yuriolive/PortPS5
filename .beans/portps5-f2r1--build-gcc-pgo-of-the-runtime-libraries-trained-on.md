---
# portps5-f2r1
title: 'Build: GCC PGO of the runtime libraries trained on perf scenes'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:25:44Z
updated_at: 2026-10-01T18:33:00Z
parent: portps5-7fqk
blocked_by:
    - portps5-52bs
    - portps5-8x1k
---

## Context

GCC PGO of our runtime libraries needs representative training runs. Blocked by: portps5-52bs (perf scenes), portps5-8x1k (build flags).

## Higher Goal

Performance track (ROADMAP, PRD 4.5) host CPU gain from profile data that stays local.

## Acceptance Criteria

- [ ] PGO training from perf scenes on the maintainer machine; profiles never committed
- [ ] release build consumes profiles when present and builds normally without them
- [ ] Before/after on perf scenes

## Out of Scope

PGO of guest code (PRD 11, v3).

## Summary of Changes

TBD
