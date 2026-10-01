---
# portps5-3asd
title: 'M1: Shader Recompiler & Synthetic Golden Corpus'
status: completed
type: task
priority: high
tags:
    - beads:portps5-2
created_at: 2026-09-30T22:53:49Z
updated_at: 2026-09-30T23:56:36Z
---

## Context

Milestone 1 needed the shader recompiler hardened and a synthetic golden corpus in hosted CI. Both landed on main: the recompiler fixes (PR #29), the synthetic corpus, replay tool and CI job (PR #31), `s_getpc_b64` absolute address (PR #36) and the Lane E small fixes (PR #60: saveexec family, scalar ops, f16 inline constants, VOP3 carry-in, MIMG UNORM). The remaining M1 recompiler item, bindless bounds from device limits, is tracked in portps5-7li6. Migrated from beads `portps5-2`.

## Higher Goal

Regressions in instruction decoding and lowering are caught on hosted CI with no GPU and no game bytecode (docs/spec/shader-recompiler.md, docs/spec/verification.md).

## Acceptance Criteria

- [x] Recompiler fixes with regression tests (PR #29, `RecompilerFixesTests`)
- [x] Synthetic golden corpus, `agc_shader_replay --golden` and the coverage gate, merged into the `build_and_test` job (PR #31, PR #38)
- [x] `s_getpc_b64` names the absolute shader address (PR #36)
- [x] Lane E instruction ports with tests (PR #60, `recompiler_ported_instruction_tests`)

## Out of Scope

Bindless bounds (moved), `EmitGetShaderBase` dispatch-time base (portps5-habc), GPU-side descriptor heap (M4).

## Summary of Changes

PRs #29, #31, #36, #38 and #60. Specs: docs/spec/shader-recompiler.md Tests and Milestones M1. Verified on main 2026-09-30: `core/shader/recompiler/tests/RecompilerFixesTests.cpp`, `RdnaPortedInstructionTests.cpp` and `golden/RecompilerGoldenTests.cpp` exist.
