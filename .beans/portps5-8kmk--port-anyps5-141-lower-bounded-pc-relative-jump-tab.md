---
# portps5-8kmk
title: 'Port AnyPS5 #141: lower bounded PC-relative jump tables in shaders'
status: todo
type: feature
created_at: 2026-10-01T21:08:49Z
updated_at: 2026-10-01T21:08:49Z
---

## Context

AnyPS5 cbf2bde (#141) lowers bounded PC-relative jump tables (s_getpc/s_setpc with a table) in the recompiler; PortPS5 has no shader-side handling (only relinker jump tables). Upstream: AnyPS5 main@709d7fe; cite the source commit in the port's commit body. Blocked by: none.

## Higher Goal

Shaders with switch-style jump tables recompile instead of failing structurization.

## Acceptance Criteria

- [ ] Port with synthetic golden shaders covering in-range and out-of-range tables
- [ ] Unbounded tables still rejected with a logged error
- [ ] shader-recompiler.md updated

## Out of Scope

Unbounded indirect branches.

## Summary of Changes

TBD
