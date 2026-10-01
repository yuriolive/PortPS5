---
# portps5-lvph
title: 'CI: Pin SHA256 of Mesa lavapipe and Vulkan runtime downloads'
status: todo
type: task
priority: low
created_at: 2026-10-01T03:00:00Z
updated_at: 2026-10-01T03:00:00Z
---

## Context

The `driver_lavapipe` job (bean portps5-ekx3) logs the SHA256 of the downloaded Mesa archive and Vulkan runtime but does not verify them.

## Higher Goal

Supply-chain integrity equal to the MinGW toolchain download.

## Acceptance Criteria

- [ ] Hashes copied from a green run into a checked-in file and verified before install

## Out of Scope

Changing the pinned versions.

## Summary of Changes

TBD
