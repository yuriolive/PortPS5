---
# portps5-ftci
title: Initialize configuration before frame timing
status: completed
type: task
priority: high
---

## Context

Verified review finding: Loader::Initialize has no production caller, leaving the GPU profile gate disabled. This addresses the startup portion of portps5-c06p.

## Higher Goal

Load immutable configuration before guest initializers or rendering threads run.

## Acceptance Criteria

- [x] Initialize configuration from the executable directory and param.json before guest startup
- [x] Check default, GPU-profile and invalid-config behavior with synthetic fixtures
- [x] Validate startup ordering and update configuration documentation

## Out of Scope

Copying config during conversion, remaining debug consumers and results JSON.

## Summary of Changes

Windows entry stub calls a verbatim libc startup export before guest initialization. The export reuses the metadata parser and reports configuration errors before guest code runs.

Validation: native focused runtime/profile tests and emitted-stub ordering passed; ordering regression fails against the original stub. Supported Windows validation remains blocked by the missing MinGW 15.2 toolchain; CodeRabbit review is disabled for this task.
