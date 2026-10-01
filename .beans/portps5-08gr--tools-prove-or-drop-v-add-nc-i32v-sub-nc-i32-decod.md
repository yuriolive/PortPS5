---
# portps5-08gr
title: 'Tools: prove or drop V_ADD_NC_I32/V_SUB_NC_I32 decoder aliases'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-9
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-10-01T18:33:26Z
parent: portps5-7dk3
---


## Context

Macroscope thread on PR #28 (`tools/rdna_isa.txt:708`): the opcode enum has `VAddI32`/`VSubI32` and the premise was that nothing referenced them. On main they are referenced: `RdnaVectorOpDecoder.cpp:359-360` maps VOP3 opcodes `0x30f`/`0x310` to them, and `Translation/src/VectorInstructions.cpp:21` lowers `VAddI32`. What is still missing is opcode-level proof that these are V_ADD_NC_I32 and V_SUB_NC_I32 (an ISA table mapping or a golden decode), so the badges stay honest. Migrated from beads `portps5-9`.

## Higher Goal

The decoder table matches the RDNA2 ISA, proven rather than assumed.

## Acceptance Criteria

- [ ] Golden decode or unit test pinning VOP3 `0x30f`/`0x310` to the ISA mnemonics, or an alias removal with a wontfix note
- [ ] `tools/rdna_isa.txt` and progress counters agree with the decoder

## Out of Scope

Other decoder aliases.

## Summary of Changes

TBD
