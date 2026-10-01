---
# portps5-z797
title: Verify glslang/SPIRV-Tools artifact checks claimed for the policy step
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:53:43Z
updated_at: 2026-10-01T18:53:44Z
parent: portps5-s1kj
---

## Context

build-toolchain.md says the policy step checks that glslang and SPIRV-Tools stay out of shipped artifacts; the 2026-10-01 audit renamed job to step but did not verify the check exists. Blocked by: none.

## Higher Goal

Licence risk R1 controls are real, not just documented.

## Acceptance Criteria

- [ ] Confirm the check in .github/workflows/ci.yml, or add it (symbol check on shipped binaries)
- [ ] build-toolchain.md matches

## Out of Scope

Resolving R1.

## Summary of Changes

TBD
