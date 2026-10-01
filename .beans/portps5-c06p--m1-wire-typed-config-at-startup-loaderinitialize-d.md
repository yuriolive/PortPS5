---
# portps5-c06p
title: 'M1: Wire typed Config at startup (Loader::Initialize, debug keys)'
status: todo
type: task
priority: high
created_at: 2026-09-30T23:49:31Z
updated_at: 2026-10-01T17:59:26Z
parent: portps5-r8mh
---


## Context

Config parsing and validation are landed and tested (core/libs/prx/libc/src/Config.cpp, global and per-game TOML, PORTPS5_DEBUG, toml++ pin), and core/ has no APS5_ string literals. But Loader::Initialize (Config.cpp:1316) has no production caller, so the startup path never loads config/global.toml or config/games/<titleId>.toml with the param.json title ID, and the relinker does not copy config/ at conversion time. Spec: docs/spec/configuration.md M1 row.

## Higher Goal

PRD F6 and the ROADMAP M1 item: per-game TOML drives the runtime, and the typed [debug] section is the only tracing and dump surface.

## Acceptance Criteria

- [x] Startup calls Loader::Initialize with the param.json title ID and aborts with file:line on a config error
- [ ] Conversion copies config/ to the install directory
- [ ] `debug.*` keys are consumed by the subsystems that used `APS5_*` switches upstream
- [ ] Resolved config hash, workarounds_set and debug_keys_set are available to the results JSON writer
- [ ] configuration.md M1 row and ROADMAP M1 config item ticked

## Out of Scope

display.present_mode and display.resolution_scale wiring (own bean), results JSON upload tooling.

## Summary of Changes

Startup is wired by portps5-ftci. Conversion copying, remaining debug consumers and results JSON remain open.
