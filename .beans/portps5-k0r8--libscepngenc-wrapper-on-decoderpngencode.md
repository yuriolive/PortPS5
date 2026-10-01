---
# portps5-k0r8
title: libScePngEnc wrapper on Decoder::Png::Encode
status: todo
type: task
priority: low
created_at: 2026-09-30T23:51:42Z
updated_at: 2026-10-01T18:33:28Z
parent: portps5-7dk3
---


## Context

The shared encoder exists and is tested (core/Decoder/Png), but the libScePngEnc library wrapper is not ported; upstream dropped its stb-based wrapper. Spec: docs/spec/image-codecs.md Milestones.

## Higher Goal

Complete the image codec set if a gate title imports libScePngEnc.

## Acceptance Criteria

- [ ] Only start after an import inventory shows a gate title needs it
- [ ] Exports APS5_VABI and noexcept, unsupported states abort via Unsupported()
- [ ] GoogleTest for handles, argument errors and round trip

## Out of Scope

16-bit PNG, MJPEG.

## Summary of Changes

TBD
