---
# portps5-j65i
title: 'CodeQL deep build fails: portps5_add_gtest undefined with BUILD_TESTING=OFF'
status: completed
type: bug
priority: normal
created_at: 2026-10-01T00:23:26Z
updated_at: 2026-10-01T00:23:37Z
---

## Context
CodeQL main-push build configures with BUILD_TESTING=OFF. Root CMakeLists only defines portps5_add_gtest when testing is ON, but core/Decoder/{Jpeg,Png} call it unconditionally: 'Unknown CMake command portps5_add_gtest' (run 36793437757).

## Higher Goal
Configure must succeed with tests off so the deep CodeQL scan on main stays green.

## Acceptance Criteria
- [x] cmake -DBUILD_TESTING=OFF configures with no errors
- [x] Fix is a no-op stub for the test helpers (PR CodeQL uses build-mode none, so the deep build is only exercised on main after merge)

## Out of Scope
github-advanced-security Copilot model error (GitHub-side setting).

## Summary of Changes
- CMakeLists.txt: define no-op portps5_add_gtest/portps5_add_test when BUILD_TESTING is OFF.
- Verified locally: cmake -DBUILD_TESTING=OFF configures with 0 errors.
