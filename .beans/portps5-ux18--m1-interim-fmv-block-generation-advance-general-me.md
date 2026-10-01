---
# portps5-ux18
title: 'M1: Interim FMV block-generation advance (general mechanism)'
status: todo
type: feature
priority: normal
created_at: 2026-09-30T23:52:43Z
updated_at: 2026-10-01T17:59:28Z
parent: portps5-4ut1
---


## Context

AnyPS5 main writes Bink planes back through an adjacent-generation rule that was title-tuned. The spec keeps a general mechanism, adjacent block-generation advance, with no switch and no title reference, for M1 to M2, replaced by MarkWritten tracking in M3. It is not implemented on main (no adjacent-generation code under libSceAgcDriver/Graphics). Specs: docs/spec/video-fmv.md, docs/spec/gpu-driver.md.

## Higher Goal

FMV correctness for Demon's Souls-class titles until general write tracking lands.

## Acceptance Criteria

- [ ] Mechanism implemented in the texture write-back path with no env switch
- [ ] Synthetic test with two planes in one 64 KiB block
- [ ] video-fmv.md M1 row and ROADMAP M1 FMV item ticked

## Out of Scope

General block-generation tracking (M3).

## Summary of Changes

TBD
