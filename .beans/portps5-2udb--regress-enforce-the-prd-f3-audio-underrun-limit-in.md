---
# portps5-2udb
title: 'regress: enforce the PRD F3 audio-underrun limit in fail_reasons'
status: todo
type: bug
priority: normal
created_at: 2026-10-01T20:18:34Z
updated_at: 2026-10-01T20:18:35Z
parent: portps5-dbpx
---

## Context

PRD F3 allows at most 1 audio underrun per 10 minutes, and docs/spec/audio.md states that limit, but `tools/regress_metrics.py` `fail_reasons` never checks `audio_underruns_per_10min`, so a run above the limit still reports `pass` (found in PR #92 review). Blocked by: none.

## Higher Goal

The results verdict enforces every PRD bar that telemetry measures.

## Acceptance Criteria

- [ ] `fail_reasons` adds `audio_underruns_per_10min` when the rate exceeds 1 (full runs and regressions)
- [ ] pytest: a synthetic result at 1.0 passes and one above 1.0 fails
- [ ] verification.md section 4 pass rule lists the limit and drops the "not enforced yet" note

## Out of Scope

A/V offset enforcement (±80 ms, separate rule).

## Summary of Changes

TBD
