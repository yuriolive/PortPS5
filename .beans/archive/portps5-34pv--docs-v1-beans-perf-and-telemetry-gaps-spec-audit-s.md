---
# portps5-34pv
title: 'docs: v1 beans, perf and telemetry gaps, spec audit sync, beyond-2.0 scope'
status: completed
type: task
priority: normal
created_at: 2026-10-01T18:54:41Z
updated_at: 2026-10-01T18:54:41Z
---

## Context

After PRs #89–#91, three gaps remained. The specs and side docs were stale against `main@1c65844a`: the 2026-09-30 sync predates PRs #53, #62, #64, #66–#68, #70, #72–#74, #76, #78–#80 and #82–#88. Most v1 ROADMAP items had no bean. The performance plan was missing state-of-the-art items: native shader optimization, async pipeline compile, an upload engine, frame pacing, a profiler, build flags, PGO, perf scenes, microbenchmarks and a barrier pass. Several telemetry events documented in the contract had no producer. Blocked by: none. Stacked on PR #91.

## Higher Goal

Every v1 task is a bean under a milestone, with blockers recorded so parallel work is visible. Every spec matches `main`. The performance and telemetry plan covers what a state-of-the-art translation layer needs. Scope beyond 2.0 is written down with a reason for each exclusion.

## Acceptance Criteria

- [x] Milestone beans M0–M6. Every unticked v1 ROADMAP item names a bean, apart from five container lines whose sub-items carry them. Every open bean has a parent, a structured body and its blockers.
- [x] Performance track: P0 gets telemetry completeness and measurement tools; new P4 "Compiler and host"; 18 new parallel-lane rows.
- [x] Telemetry gap beans (crash, run metadata, memory, stall attribution, boot timing, compile time, dialogs, config copy) and verification.md §4.4 entries for them.
- [x] PRD:
  - V7 PC enhancement layer, with risks V-R5 and V-R6;
  - §11 lists v3 candidates and permanent exclusions, with the local-only principle and the AI-research guardrails;
  - the flip-rate override is in 2.0 M12 as experimental.
- [x] ROADMAP Part II M12 and Part III. spec/README.md target architecture by version.
- [x] Spec audit fixes from four parallel audits applied after re-verification against code. Sync dates bumped to 2026-10-01. All nine headings present; no broken links.
- [x] Open questions added: gpu-driver 13–14 (renumbered after #75 added 12), guest-memory 7, pipeline-cache 5, shader-recompiler 14.
- [x] Bugs found during the audit filed: `ojy1`, `vtlf`, `tuet`, `ou5t`, `z797`, `t4yz`.

## Out of Scope

- Implementing any bean.
- Fixing red `main` CI (beans `portps5-sjuv` and `portps5-3maf`, in their own session).

## Summary of Changes

- **PRD.md:** V7, V-R5, V-R6, §11 and the §5 wording.
- **ROADMAP.md:**
  - bean IDs on the v1 items;
  - P0 additions and the new P4;
  - lanes;
  - Part II M12 and Part III;
  - F9 traceability.
- **spec/README.md:** target architecture.
- **Specs touched by the audit fixes and new open questions:** gpu-driver, shader-recompiler, video-fmv, image-codecs, verification, audio, input, save-data, configuration, guest-memory, threading, libc, relinker, build-toolchain, pipeline-cache.
- **Side docs:** TESTING.md, TechnicalDebt.md, workarounds.md, README.md, ci.yml (comment only), and the build-and-test and compat-result skills.
- **.beans/:** about 80 new or updated beans.
