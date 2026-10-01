---
# portps5-3b4e
title: tools/regress.py prepare does not copy config/, so config_sha256 hashes {}
status: todo
type: bug
priority: normal
created_at: 2026-10-01T18:31:57Z
updated_at: 2026-10-01T18:31:57Z
parent: portps5-r8mh
---

## Context

regress.py prepare docstring says it copies config/ but it doesn't; results carry the hash of an empty config. Blocked by: none.

## Higher Goal

Results JSON config hash matches the config the run used.

## Acceptance Criteria

- [ ] prepare copies config/global.toml and config/games/<titleId>.toml into the install dir
- [ ] pytest: hash of a synthetic config is non-empty and matches
- [ ] Docstring matches behaviour

## Out of Scope

Runtime config loading (portps5-c06p).

## Summary of Changes

TBD
