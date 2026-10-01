---
# portps5-sjuv
title: 'Fix main CI: libkernel imports C++-mangled libc save-data helpers that are not named exports'
status: todo
type: bug
priority: critical
created_at: 2026-10-01T18:31:09Z
updated_at: 2026-10-01T18:33:25Z
parent: portps5-7dk3
---

## Context

prx_cross_import_check (PR #68) fails on main since #53 and #68 merged together (CI run 36905103154): libkernel.prx imports _Z13MountSaveData..., _Z19DefaultSaveDataRootB5cxx11v, _Z19MountGuestDirectory..., _Z23ResolveGuestPathCheckedPKc from libc.prx, which libc does not export by name (declared in libc/include/General.hpp; called from libkernel AppMetadata.cpp and File/src/Open.cpp). Blocked by: none.

## Higher Goal

main CI green; every cross-prx call goes through a declared, verbatim _nid_no_patch C export (docs/spec/build-toolchain.md).

## Acceptance Criteria

- [ ] The four helpers are exposed as verbatim C exports (or moved into libkernel), with APS5_VABI where guest-reachable rules apply
- [ ] prx_cross_import_check passes on ctest --preset ci
- [ ] A test fails if a C++-mangled cross-prx import reappears (the check itself)

## Out of Scope

Other cross-prx API changes.

## Summary of Changes

TBD
