---
# portps5-habc
title: SPIR-V EmitGetShaderBase emits constant zero (dispatch-time shader base)
status: todo
type: task
priority: normal
created_at: 2026-09-30T23:11:48Z
updated_at: 2026-10-01T18:33:25Z
parent: portps5-7dk3
---

## Context

Follow-up to PR #36. `EmitGetShaderBase` (SpirvBackend/src/SpirvModuleEmitter.cpp) still emits constant zero, so SPIR-V consumers of an `s_getpc_b64` value see offset-only while the SRT/resource-tracker path names the real base. Tracked in docs/spec/shader-recompiler.md Open questions #3.

## Higher Goal

Correct `s_getpc_b64` results in SPIR-V output, matching the SRT path.

## Acceptance Criteria

- [ ] Dispatch-time shader base supplied to SPIR-V consumers (see spec for design)
- [ ] Regression test on a synthetic shader
- [ ] Spec updated

## Out of Scope

Title-specific handling.

## Summary of Changes

TBD
