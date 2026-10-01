---
# portps5-huyy
title: 'Survey mode: [debug] survey_unsupported logs, skips and summarises every unsupported site'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T21:18:58Z
updated_at: 2026-10-01T21:18:59Z
parent: portps5-7dk3
---

## Context

Abort-on-first-failure finds one gap per run. Survey mode (docs/spec/README.md global policy, configuration.md `debug.survey_unsupported`, verification.md 'Survey runs') logs each distinct unsupported draw, dispatch, shader or export once, skips it, counts it, and writes one capped summary at exit. Runs with it are always `fail`. Ideas from AnyPS5 #192 (memo of validation failures, so repeats are reported once) and #165 (one write per report, capped dumps; MinGW stderr writes per character). Blocked by: none.

## Higher Goal

Find every missing piece of a title in one run, without weakening the pass rule.

## Acceptance Criteria

- [ ] Config key parsed and validated; default off
- [ ] Unsupported() consults it: when on, log once per site key (kind + library/NID or opcode or reason + module-relative offset), count, and return the documented failure value instead of aborting; when off, unchanged
- [ ] Driver draw/dispatch rejection and recompiler failures use the same path (memoised per shader hash)
- [ ] Exit summary written in one call, capped (per-kind counts + first N sites); never includes game strings
- [ ] GoogleTest: on/off behaviour, dedupe, cap, summary format; death test that off still aborts
- [ ] Results JSON of a survey run is fail (existing debug-key rule)

## Out of Scope

Using survey output to pass a gate; any automatic skipping in normal runs.

## Summary of Changes

TBD
