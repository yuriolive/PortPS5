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

build-toolchain.md says the policy step checks that glslang and SPIRV-Tools stay out of shipped artifacts; the 2026-10-01 audit renamed job to step but did not verify the check exists. Blocked by: none.

## Higher Goal

Licence risk R1 controls are real, not just documented.

## Acceptance Criteria

- [x] Confirmed: the policy step runs `nm -D` on libSceAgcDriver.prx and fails on glslang symbols (ci.yml:171-179)
- [ ] Add a SPIRV-Tools presence report (informational, not failing) so the R1 status can be recorded
- [ ] build-toolchain.md matches

## Out of Scope

Resolving R1.

## Summary of Changes

TBD
