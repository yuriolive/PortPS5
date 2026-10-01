---
# portps5-j5uf
title: Port libSceJson2 from AnyPS5 (missing; needed at load by the TMNT dump)
status: todo
type: feature
created_at: 2026-10-01T20:53:13Z
updated_at: 2026-10-01T20:53:13Z
---

## Context

PortPS5 has no `core/libs/prx/libSceJson2`; AnyPS5 has a 737-line GPL-2.0 module. A locally owned TMNT dump's `eboot.bin` references `libSceJson2`, so it cannot load. Upstream source: AnyPS5 main@709d7fe (pulled 2026-10-01); cite the source commit in the port's commit body (.agents/rules/git-workflow.md). Blocked by: none.

## Higher Goal

The TMNT gate can load; the module follows PortPS5 error rules.

## Acceptance Criteria

- [ ] Port the module; replace every `throw` with SCE return codes or the `Unsupported()` abort path
- [ ] APS5_EXPORT_FN on exports; file headers per cpp-style
- [ ] GoogleTest for parse, accessor and error return codes
- [ ] Import inventory for the TMNT dump shows libSceJson2 resolved

## Out of Scope

Other JSON libraries.

## Summary of Changes

TBD
