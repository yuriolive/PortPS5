---
# portps5-2k6d
title: 'Perf P5: full guest-texture compare on every TextureCache lookup'
status: todo
type: task
priority: normal
created_at: 2026-10-01T19:21:55Z
updated_at: 2026-10-01T19:21:56Z
parent: portps5-7fqk
blocked_by:
    - portps5-421p
---

## Context

PRD §4.5 invariant P5: `TextureCache::Get` compares the whole guest texture on every lookup, hits included (docs/spec/gpu-driver.md Current state). PR #75 makes the compare cheaper (SSE2 `BytesEqual`). PR #77 skips it when `IWriteTracker::Collect` proves no change. Both PRs carry their slice plan in bean `portps5-pdc1`, which reaches `main` with #75. This bean tracks the invariant violation itself until it is gone. Blocked by: portps5-421p (the #77 shortcut takes effect only once the runtime instantiates the write tracker).

## Higher Goal

Per-draw CPU cost no longer grows with guest texture size for unchanged textures (PRD §4.5 P5).

## Acceptance Criteria

- [ ] #75 and #77 landed, or closed with reasons
- [ ] The write tracker is wired (portps5-421p), so unchanged textures skip the compare
- [ ] Before/after per-lookup cost on the release preset, recorded as results JSON
- [ ] The P5 line is removed from gpu-driver.md Current state, or the remaining cost is justified there

## Out of Scope

Resident render-target sampling (P1, portps5-r7qk).

## Summary of Changes

TBD
