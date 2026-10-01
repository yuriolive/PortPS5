---
# portps5-uqu5
title: 'Recompiler: decode s_bcnt0/ff0_b64 and _b32 saveexec forms'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:50:46Z
updated_at: 2026-09-30T23:50:46Z
---

## Context

s_bcnt0_i32_b64, s_ff0_i32_b64 and the _b32 forms of s_or/xor/andn2_saveexec are still undecoded after the Lane E port (PR #60). Upstream _a sample aliases, _cl LOD clamp and MRT export component masks need ISA verification and device-feature plumbing first. Spec: docs/spec/shader-recompiler.md Open question 8.

## Higher Goal

Every scalar opcode a gate title shader can use decodes or aborts loudly through Unsupported().

## Acceptance Criteria

- [ ] Opcodes decoded with hand-assembled synthetic tests, or recorded as not needed with evidence
- [ ] Golden corpus coverage gate still green

## Out of Scope

Sample alias, LOD clamp and MRT mask support.

## Summary of Changes

TBD
