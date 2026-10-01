---
# portps5-9s7e
title: 'M2: Retile depth to guest memory and upload never-cleared depth'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T00:56:19Z
updated_at: 2026-10-01T17:59:27Z
parent: portps5-4ut1
---


## Context

PR #55 landed the host depth/stencil surface (Graphics/src/DepthSurface.cpp) but not the path back to guest memory: CPU or shader reads of a depth buffer need a depth-aware tile/detile pass, and a surface that was never cleared has no guest-memory upload, so a draw that depth-tests such a surface is rejected with a logged error. Spec: docs/spec/gpu-driver.md (Depth surface, M2 row).

## Higher Goal

Depth surfaces readable by the guest and drawable without a prior clear, with no silent skips.

## Acceptance Criteria

- [ ] Depth-aware retile shader or CPU path for depth reads
- [ ] Guest-memory upload for never-cleared surfaces
- [ ] Rejection removed with GoogleTest coverage on lavapipe
- [ ] gpu-driver.md M2 row and ROADMAP M2 retile item ticked

## Out of Scope

Partial clears, MSAA/array/mip depth, HTILE-aware paths.

## Summary of Changes

TBD
