---
# portps5-jbdl
title: Fix failing AGC driver lavapipe tests
status: completed
type: task
priority: normal
created_at: 2026-10-01T02:45:33Z
updated_at: 2026-10-01T02:45:41Z
---

## Context
`agc_driver_bda_device`, `agc_driver_graphics`, `agc_driver_pm4` and `agc_driver_recompiler` failed on `main`. All four were stale or wrong tests (plus one real defect), not product regressions. The other listed tests (`agc_command`, `agc_recorder_tests`, `agc_driver_depth_*`, `agc_replay_golden_*`) pass once the whole tree is built: they were only unbuilt, and carry label `lavapipe`, so `ctest --preset ci` (label `unit`) never selects them.

## Higher Goal
Make the driver `lavapipe` suites trustworthy: green on a clean build, every assertion still meaningful, so they can back the future `driver-lavapipe` CI job.

## Acceptance Criteria
- [x] bda_device: contract test builds blobs that hit the version check and the signature check separately; device checks run
- [x] pm4: `MemoryAccessScope` hook state lives once in the driver (not header-inline), so a test's scope reaches the DLL
- [x] recompiler: stale "Recompile not implemented" test replaced by GoogleTest on a real compute dispatch plus a missing-register negative test
- [x] "missing register at DWORD 0x..." prints hex
- [x] graphics: stale "not implemented" expectations replaced with the real rejections; mock limits allow sampler bindings; sampler LOD test matches the 12-bit field
- [x] `ctest --test-dir build/ci -j4` passes (676 tests), `tools/check_comments.py --base origin/main` OK

## Out of Scope
Converting the remaining hand-rolled runners (`Graphics.cpp`, `Pm4.cpp`, `BdaDevice.cpp`) to GoogleTest; adding the `driver-lavapipe` CI job (bean portps5-ekx3); the removed `draw`/`vertex` argv modes of the old recompiler test.

## Summary of Changes
- `tests/BdaContracts.cpp`: separate bad-signature and bad-version blobs.
- `Execution/include/MemoryAccessScope.hpp`, `Execution/src/GuestMemory.cpp`: single DLL-owned thread-local hook (PE gives each module its own inline thread_local).
- `Graphics/src/ShaderInputState.cpp`: hex register offset in message.
- `tests/Recompiler.cpp` + CMake: GoogleTest via `portps5_add_gtest`; dropped the argv `draw`/`vertex` modes, which asserted the removed stub and were never run by ctest.
- `tests/Graphics.cpp`, `tests/GuestSamplerResource.cpp`, `tests/Pm4.cpp`: corrected expectations, split compound assertion.
- `docs/spec/gpu-driver.md`: test and hook notes.
