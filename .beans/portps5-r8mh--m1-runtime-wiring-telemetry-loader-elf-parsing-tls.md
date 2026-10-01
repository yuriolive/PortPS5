---
# portps5-r8mh
title: 'M1: Runtime Wiring & Telemetry (Loader, ELF parsing, TLS, headless verification flag, telemetry hooks)'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-4
created_at: 2026-09-30T22:53:49Z
updated_at: 2026-09-30T23:56:54Z
---

## Context

Epic for M1 runtime wiring: loader, ELF parsing, TLS, the headless verification flag and telemetry hooks. The pieces that exist: Config parsing and validation with a per-game TOML schema (`libc/src/Config.cpp`), verbatim cross-prx `Loader` exports (PRs #44, #47), entry-stub clean exit on load failure, offline services. Not wired: `Loader::Initialize` has no production caller (portps5-c06p), display keys are ignored (portps5-dtwf), and there is no frame-time log, watchdog or structured log (portps5-f9a3). Migrated from beads `portps5-4`.

## Higher Goal

A converted title starts with its per-game config and produces the telemetry the verification protocol needs (PRD F6, F9).

## Acceptance Criteria

- [x] Config schema, validation and `[debug]` landed with `policy` enforcement
- [x] `Config::Loader` verbatim exports and clean loader failure exit (PRs #44, #47)
- [ ] Startup loads config for the `param.json` title ID: portps5-c06p
- [ ] Display keys wired: portps5-dtwf
- [ ] Telemetry: portps5-f9a3

## Out of Scope

Results JSON upload tooling (portps5-3m3u).

## Summary of Changes

Partial. Specs: docs/spec/configuration.md M1 row, docs/spec/verification.md.
