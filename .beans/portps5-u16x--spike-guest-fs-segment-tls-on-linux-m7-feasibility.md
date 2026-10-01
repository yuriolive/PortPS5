---
# portps5-u16x
title: 'Spike: guest fs-segment TLS on Linux (M7 feasibility)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:12:42Z
updated_at: 2026-10-01T18:15:43Z
parent: portps5-epoi
---

## Context

Guest code addresses TLS through the fs segment; glibc uses fs for host TLS (docs/spec/host-platform.md open question 1, PRD V-R2). M7 is not feasible as planned until this is answered. Blocked by: none; it can run at any time.

## Higher Goal

Decide whether native Linux is a backend swap or needs relinker or entry-path work, before M7 commits.

## Acceptance Criteria

- [ ] Inventory fs: uses in one converted gate title (counts only, no game bytes committed)
- [ ] Prototype on a synthetic guest: arch_prctl/wrfsbase switching vs relinker rewrite; measure switch cost
- [ ] Recommendation recorded in host-platform.md open question 1

## Out of Scope

Implementing the chosen option (M7).

## Summary of Changes

TBD
