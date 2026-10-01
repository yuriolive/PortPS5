---
# portps5-e7aa
title: 'Port AnyPS5 #132: CB fast-clear eliminate and DCC decompress passes'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T20:53:15Z
updated_at: 2026-10-01T20:53:15Z
parent: portps5-4ut1
---

## Context

PortPS5 rejects these colour-buffer passes; AnyPS5 5ab2fb0 (#132, ~440 lines) runs them within its DCC model. Upstream source: AnyPS5 main@709d7fe (pulled 2026-10-01); cite the source commit in the port's commit body (.agents/rules/git-workflow.md). Blocked by: none.

## Higher Goal

Draws that request FCE or DCC decompress run instead of failing.

## Acceptance Criteria

- [ ] Port adapted to the always-decompressed host image model (gpu-driver OQ 2)
- [ ] Unit tests on register decode; lavapipe pass test
- [ ] gpu-driver.md updated

## Out of Scope

Metadata-aware HTILE/DCC (M5).

## Summary of Changes

TBD
