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
- [ ] Where present_id/present_wait exist, the `frame` record carries `present_to_display_ms` (number, milliseconds: from `vkQueuePresentKHR` for that frame's `present_id` until `vkWaitForPresentKHR` returns; omitted when the extensions are missing)
- [ ] The results JSON gets `present: { to_display_ms_p50, to_display_ms_p99, interval_jitter_ms }` (milliseconds; jitter is p99 minus p50 of the intervals between successive present completions, over the same non-stall frames as `fps`; `tools/regress_metrics.py` computes them from the `frame` records and omits `present` when no record carries the field), with a pytest over synthetic logs

## Out of Scope

Frame-rate unlocks (PRD 11).

## Summary of Changes

TBD
