---
# portps5-epoi
title: 'v2 readiness: seams built in v1 so v2 plugs in without rewrites'
status: todo
type: epic
priority: normal
created_at: 2026-10-01T18:12:41Z
updated_at: 2026-10-01T18:15:26Z
---

## Context

2.0 targets an open-world AAA tier (PRD §10). Several 2.0 mechanisms need interfaces that 1.0 code would otherwise hard-wire: host OS calls, the driver's recording model, cache residency, guest memory views, device capabilities, recompiler op coverage, cache keys and file I/O. ROADMAP Part I 'v2 seams in v1' lists them with their parallel lanes. Blocked by: none (children carry their own blockers).

## Higher Goal

2.0 plugs into seams built and tested in 1.0 instead of rewriting 1.0 subsystems. Design for 2.0, implement for 1.0.

## Acceptance Criteria

- [ ] Children completed: portps5-37j0, portps5-qfac, portps5-hkwd, portps5-hps3, portps5-gkef, portps5-l77s, portps5-jehk, portps5-j4e1
- [ ] Spikes reported: portps5-u16x, portps5-mdu8
- [ ] pipeline-cache EnvKey feature hash landed with portps5-8gdr

## Out of Scope

Any 2.0 feature (Linux backend, residency under pressure, RT, mesh, decompression).

## Summary of Changes

TBD
