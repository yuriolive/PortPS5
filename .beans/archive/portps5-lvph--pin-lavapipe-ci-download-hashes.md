---
# portps5-lvph
title: 'CI: Pin SHA256 of Mesa lavapipe and Vulkan runtime downloads'
status: completed
type: task
priority: low
created_at: 2026-10-01T03:00:00Z
updated_at: 2026-10-01T03:00:00Z
---

## Context

The `driver_lavapipe` job (bean portps5-ekx3) logged but did not verify the SHA256 of the downloaded Mesa archive and Vulkan runtime. The first version also tried the LunarG installer, which hangs on hosted runners; the job now uses LunarG's `vulkan-runtime-components.zip`.

## Higher Goal

Supply-chain integrity equal to the MinGW toolchain download.

## Acceptance Criteria

- [x] Digests pinned in `ci.yml` (`VULKAN_RT_SHA256`, `MESA_SHA256`) and verified before extraction, including files restored from the actions cache

## Out of Scope

Changing the pinned versions.

## Summary of Changes

- `.github/workflows/ci.yml`: `Assert-Sha256` runs on both downloads and deletes the file and fails the job on mismatch, before `Expand-Archive` or `7z`.
- Digests come from two independent downloads that matched (`vulkan-runtime-components.zip` e0429f91..., `mesa3d-24.3.4-release-msvc.7z` 7ebc711a...). Neither upstream publishes checksums, so these are first-use pins, not vendor-signed values: bump them with `MESA_VERSION` / `VULKAN_RT_VERSION` and re-check by hand.
