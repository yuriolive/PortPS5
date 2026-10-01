---
# portps5-71o2
title: 'Presenter: final composition pass hook and swapchain colour-space selection (v1 seam)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:23:42Z
updated_at: 2026-10-01T18:33:02Z
parent: portps5-epoi
---

## Context

The presenter copies the final image to the swapchain; 2.0 M12 needs a composition pass (spatial upscale, HDR, frame generation) and colour-space choice. Blocked by: portps5-l77s (capability table).

## Higher Goal

M12 enhancements plug into an existing presenter hook (ROADMAP v2 seams).

## Acceptance Criteria

- [ ] Presenter has one composition-pass hook; v1 implementation is a copy
- [ ] Swapchain colour space chosen from the capability table (SDR only in v1)
- [ ] lavapipe test that the hook path matches the direct copy byte for byte

## Out of Scope

Any enhancement (M12).

## Summary of Changes

TBD
