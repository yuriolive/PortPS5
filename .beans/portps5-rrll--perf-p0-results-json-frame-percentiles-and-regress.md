---
# portps5-rrll
title: 'Perf P0: results JSON frame percentiles and regress compare'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T18:03:34Z
updated_at: 2026-10-01T18:10:17Z
parent: portps5-7fqk
---

## Context

`.agents/rules/testing.md` requires before/after measurements for performance claims, but `tools/regress.py` has no way to compare two results, and the results JSON has no frame-time percentiles. Target design is in docs/spec/verification.md §4.4 ("Frame-time percentiles", "Baseline compare"). Blocked by: none. It works on the existing fields; the breakdown and counter fields are added by portps5-hfiw and portps5-aifo, and the compare picks them up as they appear.

## Higher Goal

A performance PR states its effect as a mechanical compare against a stored baseline result, with refusal on mismatched run conditions.

## Acceptance Criteria

- [ ] `frame_ms: { p50, p90, p99 }` over non-stall frames
- [ ] `tools/regress.py compare --baseline --result`: per-field deltas, refusal on mismatched title pin / run_type / host_tier / resolution / pipeline_cache / config_sha256, non-zero exit on regression thresholds
- [ ] pytest on synthetic results JSON (match, refusal, each threshold)
- [ ] verification.md §4.4 checkboxes ticked; thresholds updated once run-to-run noise is measured

## Out of Scope

Uploading results, CI-side perf gating (no GPU in hosted CI).

## Summary of Changes

TBD
