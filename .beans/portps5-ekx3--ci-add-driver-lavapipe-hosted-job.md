---
# portps5-ekx3
title: 'CI: Add driver-lavapipe hosted job'
status: todo
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

- [ ] Job installs lavapipe on the Windows runner or uses a Linux job and runs ctest -L lavapipe
- [ ] Tests that need a device skip themselves cleanly when none exists
- [ ] verification.md and build-toolchain.md list the job

## Out of Scope

GPU-only visual tests.

## Summary of Changes

TBD
