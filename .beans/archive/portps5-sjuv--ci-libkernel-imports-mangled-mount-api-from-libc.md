---
# portps5-sjuv
title: 'CI: libkernel imports mangled mount-table API from libc.prx'
status: completed
type: bug
priority: critical
created_at: 2026-10-01T19:00:00Z
updated_at: 2026-10-01T19:00:00Z
---

## Context

`main` CI is red at 1c65844a (run 36905103154, Build & Test): the ctest `prx_cross_import_check` (`tools/check_prx_imports.py`, PR #68) reports four named imports of `libkernel.prx` missing from `libc.prx`: the mangled `MountSaveData`, `DefaultSaveDataRoot`, `MountGuestDirectory` and `ResolveGuestPathChecked`. PR #53 (/savedata0 mount) declared them as plain C++ functions in `core/libs/prx/libc/include/General.hpp` and calls them from libkernel (`AppMetadata/src/AppMetadata.cpp`, `File/src/Open.cpp`). `nid_patcher libc` hashes undecorated C++ exports, so the patched libkernel.prx would fail to load with GetLastError 127 at boot.

## Higher Goal

Cross-prx host APIs keep verbatim names (docs/spec/build-toolchain.md export rules), so every prx loads and the import check stays green.

## Acceptance Criteria

- [x] The mount-table API (ResolveGuestPathChecked, MountGuestDirectory, UnmountGuestDirectory, DefaultSaveDataRoot, MountSaveData) is exported as verbatim `_nid_no_patch` C symbols with inline forwarders (the `Unsupported` pattern)
- [x] `prx_cross_import_check` passes on the patched libs directory
- [x] Existing save-data mount GoogleTests pass unchanged
- [x] docs/spec/build-toolchain.md records the third occurrence

## Out of Scope

Making `nid_patcher libc` run with `--preserve-exports`; moving the mount table into libkernel.

## Summary of Changes

- `core/libs/prx/libc/include/General.hpp`, `src/General.cpp`: ResolveGuestPathChecked, MountGuestDirectory, UnmountGuestDirectory, DefaultSaveDataRoot and MountSaveData are now `extern "C"` `*_nid_no_patch` exports; the old spellings are inline forwarders, so libkernel call sites and tests/filesystem tests are unchanged.
- `docs/spec/build-toolchain.md`: third occurrence recorded under cross-prx host APIs.
- Regression test: the existing ctest `prx_cross_import_check` (failed on main@1c65844a, passes now). Local `ctest --preset ci`: 808/808 passed.
