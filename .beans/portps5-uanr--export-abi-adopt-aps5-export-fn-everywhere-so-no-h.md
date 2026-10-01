---
# portps5-uanr
title: 'Export ABI: adopt APS5_EXPORT_FN everywhere so no host exception crosses APS5_VABI'
status: todo
type: task
priority: normal
created_at: 2026-10-01T20:31:35Z
updated_at: 2026-10-01T20:31:36Z
parent: portps5-7dk3
---

## Context

`APS5_EXPORT_FN` (declares an export `APS5_VABI` and `noexcept`) exists but has no call sites: `libSceAgc` registers through the raw `APS5_EXPORT`, `libSceSysmodule/Export.cpp` still throws, and `docs/TechnicalDebt.md` "Host exceptions" counts the `throw std::` sites per module. A host exception that reaches an export is swallowed by a guest `catch(...)` through the shared unwinder, or terminates the process inside a `noexcept` export, instead of taking the logging abort path (docs/spec/threading.md Error policy). Found in PR #92 review. Blocked by: none.

## Higher Goal

No host exception crosses an `APS5_VABI` boundary (.agents/rules/cpp-style.md Errors).

## Acceptance Criteria

- [ ] Every guest-callable export uses `APS5_EXPORT_FN`
- [ ] Throws reachable from exports become SCE/POSIX return codes or `Unsupported()` aborts
- [ ] The `policy` step rejects a raw `APS5_EXPORT` (build-toolchain.md)
- [ ] Death tests for representative former throw paths

## Out of Scope

Host-only tools (`core/libs/nid`, the relinker).

## Summary of Changes

TBD
