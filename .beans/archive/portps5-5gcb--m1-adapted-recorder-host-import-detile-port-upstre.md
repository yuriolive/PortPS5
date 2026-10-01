---
# portps5-5gcb
title: 'M1: Adapted Recorder + host-import + detile port (upstream PR #5 core)'
status: completed
type: task
priority: normal
tags:
    - beads:portps5-6
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-09-30T23:56:38Z
---

## Context

Upstream's Recorder, host import and detile were trial-ported on PR #28 and reverted (32 `APS5_*` switches, throws across the ABI, no driver wiring). They returned adapted: PR #49 landed `Recorder` and `HostImport` with a staging fallback and 35 GoogleTests (`agc_recorder_tests`, label `lavapipe`), and the texture detile shader exists (`Graphics/src/TextureDetiler.cpp`). Wiring them into the submit path is deliberately not part of this bean and is tracked in portps5-tiod. Migrated from beads `portps5-6`.

## Higher Goal

Serial-based submission, pending-write tracking and host import for the AGC driver (docs/spec/gpu-driver.md, "Landed in PortPS5").

## Acceptance Criteria

- [x] Adapted `Recorder` (serials, timeline semaphore, pending-write snapshot, label table, in-order completions, flush hook through `IWriteTracker::SetFlushHook`), no env switches, no throws
- [x] `HostImport` with `VK_EXT_external_memory_host`, budget, LRU and staging fallback
- [x] GPU detile in tree (`TextureDetiler`)
- [x] GoogleTests: `tests/RecorderTests.cpp`, `tests/HostImportTests.cpp`

## Out of Scope

Driver wiring (portps5-tiod), GPU-side indirect draws (M3), automatic import budget (M5).

## Summary of Changes

PR #49 (`Graphics/{include,src}/Recorder.*`, `HostImport.*`, `Context.hpp`, `VulkanDevice.cpp` feature enablement, tests, docs/spec/gpu-driver.md). Verified on main 2026-09-30 that `sceAgcDriverSubmitDcb` (`Submit/src/Dcb.cpp:31`) does not use the Recorder.
