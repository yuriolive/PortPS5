---
# portps5-jy7n
title: 'Frame pacing: present_wait/present_id, guest flip-rate pacing without Sleep jitter, FIFO-relaxed and mailbox'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:25:10Z
updated_at: 2026-10-01T21:35:32Z
parent: portps5-7fqk
blocked_by:
    - portps5-dtwf
    - portps5-w1re
---


## Context

The swapchain is FIFO and pacing follows the guest flip with no present timing; 1% low is part of the pass bar. Blocked by: portps5-dtwf, portps5-w1re.

## Higher Goal

Performance track (ROADMAP, PRD 4.5) smoother frame times at the same average fps.

## Acceptance Criteria

- [ ] VK_KHR_present_id/present_wait where supported
- [ ] Pacing to the guest flip rate without Sleep jitter (high-resolution waits)
- [ ] FIFO-relaxed and mailbox selectable through display.present_mode
- [ ] Before/after 1% low on a perf scene
- [ ] Where present_id/present_wait exist, the `frame` record carries numeric `present_to_display_ms`, and the results JSON gets present-interval jitter (p99 minus p50 of present intervals)

## Out of Scope

Frame-rate unlocks (PRD 11).

## Summary of Changes

TBD
