---
# portps5-r8mh
title: 'M1: Runtime wiring and telemetry (config at startup, display keys, telemetry call sites)'
status: todo
type: epic
priority: normal
tags:
    - beads:portps5-4
created_at: 2026-09-30T22:53:49Z
updated_at: 2026-10-01T18:00:20Z
---

## Context

Epic for M1 runtime wiring: config at startup, display keys and telemetry call sites (ROADMAP M1 config and telemetry items, docs/spec/configuration.md, docs/spec/verification.md 4.3). Migrated from beads `portps5-4`; refreshed against `main` on 2026-10-01.

Landed: Config parsing and validation with a per-game TOML schema and typed `[debug]` (`libc/src/Config.cpp`), verbatim cross-prx `Loader` exports and clean loader failure exit (PRs #44, #47), offline services, the telemetry core, watchdog and audio sampler (PR #78, portps5-f9a3), the `tools/regress.py` runner and results JSON writer (PR #76, part of portps5-3m3u).

Not wired: `Loader::Initialize` has no production caller (portps5-c06p), display keys are ignored and the swapchain is FIFO (portps5-dtwf), and nothing calls the telemetry `Start`, `NotePresent` or `NoteGuestProgress` exports (portps5-w1re).

## Higher Goal

A converted title starts with its per-game config and produces the telemetry the verification protocol needs (PRD F6, F9).

## Acceptance Criteria

- [x] Config schema, validation and `[debug]` landed with `policy` enforcement
- [x] `Config::Loader` verbatim exports and clean loader failure exit (PRs #44, #47)
- [x] Telemetry core, watchdog and audio sampler: portps5-f9a3 (PR #78)
- [ ] Startup loads config for the `param.json` title ID: portps5-c06p
- [ ] Display keys reach the driver: portps5-dtwf
- [ ] Telemetry call sites wired: portps5-w1re

## Out of Scope

Results JSON upload and frame checks (portps5-3m3u).

## Summary of Changes

Partial; see the ticked items above. Specs: docs/spec/configuration.md M1 row, docs/spec/verification.md 4.3.
