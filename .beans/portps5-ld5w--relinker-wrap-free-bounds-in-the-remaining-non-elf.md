---
# portps5-ld5w
title: 'Relinker: wrap-free bounds in the remaining non-ElfReader sites'
status: todo
type: bug
priority: normal
created_at: 2026-09-30T23:50:32Z
updated_at: 2026-10-01T00:20:57Z
---


## Context

PR #64's review found the same a + b bounds-wrap hazard outside ElfReader. Verified against main: core/relinker/relinker/src/analysis/UnusedNidFilter/EntryPointCollector.cpp:74 and RelativeRelocationIndex.cpp:129 (segOff + segSz > size), core/relinker/relinker/src/guest/GuestImageReader.cpp:65 (loop bound dynamic->Offset + dynamic->FileSize); lower risk, off + relaEntSize <= relaSize loops at relinker/src/pipeline/RelinkerPipeline.cpp:166 and :284 and RelativeRelocationIndex.cpp:54. Spec: docs/spec/relinker.md Failure modes. PR #64 has landed, so the wrap-free style is already in `ElfReader`.

## Higher Goal

Untrusted ELF header fields never reach a bounds check that can wrap.

## Acceptance Criteria

- [ ] Each listed site rewritten in the form `offset > size || len > size - offset`
- [ ] One GoogleTest per site that fails without the fix
- [ ] relinker.md failure-mode row lists the covered readers

## Out of Scope

Changing the fail-safe TranslateVirtualAddress behaviour.

## Summary of Changes

TBD
