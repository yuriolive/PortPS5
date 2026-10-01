---
# portps5-3m3u
title: 'M2: tools/regress local regression and results JSON'
status: in-progress
type: feature
priority: high
created_at: 2026-09-30T23:49:51Z
updated_at: 2026-09-30T23:49:51Z
---

## Context

tools/ holds only check_comments.py and progress.py. The local regression driver and the results JSON (schema portps5.results/1) described in docs/spec/verification.md sections 2 and 4 do not exist, so no gate title run can be recorded except by hand with the compat-result skill.

## Higher Goal

Reproducible local runs that publish only metrics, hashes and pass/fail (ROADMAP M2 tools/regress item, F9).

## Acceptance Criteria

- [ ] tools/regress runs boot (done: `prepare`/`run`), checkpoint load, frame-check and save round-trip steps (only boot and results consumption done; the others pass in through `--checks-file`)
- [x] Emits results JSON with config_sha256, workarounds_set, debug_keys_set and the FMV played rule
- [x] Schema validated by a Python pytest under tests/tools (`tests/tools/test_regress.py`)
- [x] verification.md and build-toolchain.md M2 rows updated

## Out of Scope

Automated upload to compat/results, publishing the compatibility list (M2 exit).

## Summary of Changes

Added tools/regress.py, tools/regress_metrics.py, tests/tools/test_regress.py, verification.md 4.1 and 4.2. Bean stays open for the frame-check, checkpoint and upload steps.
