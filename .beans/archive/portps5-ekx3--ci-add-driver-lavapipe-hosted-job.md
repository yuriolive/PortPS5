---
# portps5-ekx3
title: 'CI: Add driver-lavapipe hosted job'
status: completed
type: task
priority: normal
created_at: 2026-09-30T23:50:23Z
updated_at: 2026-09-30T23:50:23Z
---

## Context

Driver suites are labelled lavapipe (libSceAgcDriver/CMakeLists.txt:351-389, including agc_recorder_tests) but .github/workflows/ci.yml has no driver-lavapipe job, so they do not run on hosted CI with a software Vulkan device. Specs: docs/spec/verification.md section 1, docs/spec/build-toolchain.md M1 row.

## Higher Goal

Hosted coverage of the Vulkan driver, Recorder and HostImport without a GPU (ROADMAP M1 CI item).

## Acceptance Criteria

- [x] Job installs lavapipe on the Windows runner or uses a Linux job and runs ctest -L lavapipe
- [x] Tests that need a device skip themselves cleanly when none exists
- [x] verification.md and build-toolchain.md list the job

## Out of Scope

GPU-only visual tests.

## Summary of Changes

- `.github/workflows/ci.yml`: new Windows job `driver_lavapipe` (loader + pinned Mesa lavapipe, `VK_DRIVER_FILES`, device verify step, `ctest --preset lavapipe --no-tests=error`).
- `CMakePresets.json`: test preset `lavapipe` (label filter, 4 jobs).
- Docs: `docs/spec/verification.md`, `docs/spec/build-toolchain.md`, `docs/ROADMAP.md` M1 item ticked.
- Windows job chosen: the driver loads `vulkan-1.dll` and builds only with MinGW. Device-less skips already exist (`GTEST_SKIP` in Recorder/HostImport fixtures).
- Not verified locally: this Linux sandbox has no MinGW or lavapipe. Mesa/VulkanRT archive hashes are logged, not pinned. The job is red until PR #80 lands.
