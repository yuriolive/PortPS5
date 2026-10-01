---
# portps5-z797
title: 'Policy step: report SPIRV-Tools presence in release artifacts (glslang check confirmed)'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:53:43Z
updated_at: 2026-10-01T21:10:39Z
parent: portps5-s1kj
---


## Context

The policy step checks only glslang: it runs `nm -D` on `build/ci/core/libs/libs/libSceAgcDriver.prx` and fails on glslang symbols (ci.yml:171-180). It has no SPIRV-Tools check, and when the prx is absent it skips the check without failing or warning. build-toolchain.md was corrected to describe this. Blocked by: none.

## Higher Goal

Licence risk R1 controls are real, not just documented.

## Acceptance Criteria

- [x] Confirmed: the policy step runs `nm -D` on libSceAgcDriver.prx and fails on glslang symbols (ci.yml:171-179)
- [ ] Add a SPIRV-Tools presence report (informational, not failing) so the R1 status can be recorded
- [ ] A missing libSceAgcDriver.prx fails the policy step instead of skipping the glslang check silently
- [ ] build-toolchain.md matches

## Out of Scope

Resolving R1.

## Summary of Changes

TBD
