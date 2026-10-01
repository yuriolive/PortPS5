---
# portps5-cmyp
title: Symbolised crash reports and an in-memory flight recorder
status: todo
type: feature
created_at: 2026-10-01T21:35:20Z
updated_at: 2026-10-01T21:35:20Z
parent: portps5-etxc
blocked_by:
    - portps5-yhj7
---

## Context

The crash producer (portps5-yhj7) will write `crash { code }`. A report with raw addresses still needs manual work to tell guest code from host prx code. The relinker knows guest function ranges (FDE and symbol ranges, relinker.md), and a ring buffer of recent log and telemetry records shows what led up to the crash. Every capture, dump and report stays in the install directory on the maintainer's machine; only numeric fields may reach the results JSON (.agents/rules/legal-boundary.md).

Blocked by: portps5-yhj7.

## Higher Goal

Every crash leaves one local report that classifies every frame and names the function wherever symbols exist (module+offset otherwise).

## Acceptance Criteria

- [ ] The relinker writes `<exe>.map` with guest function ranges
- [ ] The crash handler writes `<install>/logs/crash-<t>.txt`: exception, registers, a stack walk with each address classified (guest image, prx, host) and module+offset, the last N ring-buffer records, and a minidump
- [ ] `tools/symbolize.py` resolves module+offset to names offline: guest frames from the relinker map, prx frames from prx DWARF (dev/RelWithDebInfo builds) or the prx export table, host frames (game.exe stub, MinGW runtime DLLs) from their DWARF when built with `-g`, else their PE export table; Windows system DLLs stay module+offset
- [ ] Unit tests: map writer, address classification, ring-buffer wrap

## Out of Scope

Uploading crash reports anywhere.
