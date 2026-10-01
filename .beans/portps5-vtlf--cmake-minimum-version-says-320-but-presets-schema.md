---
# portps5-vtlf
title: CMake minimum version says 3.20 but presets schema v6 needs 3.25
status: todo
type: bug
priority: normal
created_at: 2026-10-01T18:53:42Z
updated_at: 2026-10-01T18:53:42Z
parent: portps5-s1kj
---

## Context

CMakePresets.json uses schema version 6 (CMake 3.25+), while cmakeMinimumRequired and cmake_minimum_required say 3.20; docs now say 3.25+. Blocked by: none.

## Higher Goal

The declared minimum matches what actually configures.

## Acceptance Criteria

- [ ] Raise cmakeMinimumRequired and cmake_minimum_required to 3.25, or lower the presets schema
- [ ] CI configure unchanged

## Out of Scope

Toolchain pins.

## Summary of Changes

TBD
