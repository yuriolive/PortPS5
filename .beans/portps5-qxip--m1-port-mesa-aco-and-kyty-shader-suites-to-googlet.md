---
# portps5-qxip
title: 'M1: Port Mesa ACO and Kyty shader suites to GoogleTest'
status: todo
type: task
priority: normal
created_at: 2026-10-01T00:35:05Z
updated_at: 2026-10-01T18:33:24Z
parent: portps5-7dk3
---


## Context

The ROADMAP M1 recompiler ecosystem item was unticked on 2026-09-30: only RecompilerFixesTests and RdnaPortedInstructionTests exist. Missing: KytyPS5 ShaderRecompilerComputeTests, shaderCfgTests, Mesa ACO GFX10.3 decoding/DPP/SDWA/LDS/divergent-control-flow vectors, decoder TEST_P round trips and recompiler EXPECT_DEATH tests. Spec: docs/spec/shader-recompiler.md Tests.

## Higher Goal

Decoder and lowering regressions are caught on hosted CI from independent reference vectors, with no game bytecode.

## Acceptance Criteria

- [ ] Decoder round trip per opcode encoding with TEST_P
- [ ] ValidateProgram negative cases and an EXPECT_DEATH test for unresolvable opcodes
- [ ] Kyty compute and CFG suites re-expressed as GoogleTest or dropped with a written reason
- [ ] Mesa ACO vectors adapted with title IDs and licences handled per the legal rule
- [ ] ROADMAP M1 recompiler item ticked

## Out of Scope

Structurizer tier 2 and fuzz corpus (M3).

## Summary of Changes

TBD
