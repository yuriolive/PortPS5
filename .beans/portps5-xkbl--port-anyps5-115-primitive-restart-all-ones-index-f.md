---
# portps5-xkbl
title: 'Port AnyPS5 #115: primitive restart (all-ones index) for strips and lists'
status: todo
type: feature
priority: normal
created_at: 2026-10-01T20:53:14Z
updated_at: 2026-10-01T20:53:14Z
parent: portps5-4ut1
---

## Context

PortPS5 rejects any nonzero GE_MULTI_PRIM_IB_RESET_EN (`Graphics/src/State.cpp:369`). AnyPS5 fc05962 (#115) maps it to Vulkan primitive restart, with VK_EXT_primitive_topology_list_restart for lists. Upstream source: AnyPS5 main@709d7fe (pulled 2026-10-01); cite the source commit in the port's commit body (.agents/rules/git-workflow.md). Blocked by: none.

## Higher Goal

Titles that use primitive restart draw instead of being rejected.

## Acceptance Criteria

- [ ] Strips use primitiveRestartEnable; lists only with the extension, else a logged rejection
- [ ] Capability recorded (capability table portps5-l77s when it lands)
- [ ] lavapipe or unit test with an all-ones index

## Out of Scope

Other topologies.

## Summary of Changes

TBD
