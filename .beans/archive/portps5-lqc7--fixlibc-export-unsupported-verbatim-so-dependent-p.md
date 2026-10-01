---
# portps5-lqc7
title: 'fix(libc): export Unsupported verbatim so dependent prx load'
status: completed
type: task
priority: high
created_at: 2026-10-01T16:40:35Z
updated_at: 2026-10-01T16:40:35Z
---

## Context
`libScePad` and seven other prx imported the C++-mangled `Unsupported(const char*)` from `libc.prx`. `nid_patcher libc` hashes undecorated C++ exports, so the name was not exported and `libSceVideoOut` failed to load with GetLastError 127 on the Dreaming Sarah boot path (same class as the Config::Loader break).

## Higher Goal
Cross-prx host APIs keep verbatim names, and a build-time check catches a recurrence without running a game.

## Acceptance Criteria
- [x] `Unsupported_nid_no_patch` exported verbatim; `Unsupported` is an inline forwarder.
- [x] `prx_cross_import_check` ctest and `tools/check_prx_imports.py` fail on the old build (8 missing exports) and pass with the fix.
- [x] Review findings fixed with tests: empty directory, NID exemption (alphabet-only NIDs stay exempt; declared `_nid_no_patch_cut` names are checked), ruff format, Doxygen.
- [x] Converted gate title boots past prx load.

## Out of Scope
Anything after prx load (save mount, rendering).

## Summary of Changes
`General.hpp`/`General.cpp` (verbatim export), `tools/check_prx_imports.py`, `tests/tools/test_check_prx_imports.py`, `tests/CMakeLists.txt`, `docs/spec/build-toolchain.md`. PR #68.
