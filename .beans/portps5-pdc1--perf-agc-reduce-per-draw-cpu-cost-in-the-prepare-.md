---
# portps5-pdc1
title: 'perf(agc): reduce per-draw CPU cost in the prepare path'
status: in-progress
type: task
priority: high
created_at: 2026-10-01T03:00:00Z
updated_at: 2026-10-01T03:00:00Z
---

## Context

Converted titles miss the 60 Hz flip rate in draw-heavy scenes. Slow frames carry about 53 draws against about 16 in light frames, and `Driver.Draw.total` is about 30 ms per slow frame (`ShaderResources.bindings` about 12 ms, fence waits 6 to 7 ms, uploads about 3 ms each, `TextureCache::Get` about 0.15 ms per lookup including hits because it compares the whole guest texture every draw). Measure on the `release` preset only. See `docs/spec/gpu-driver.md` "Per-draw CPU cost".

## Higher Goal

Hold the 60 Hz flip rate on the M2 gate titles (ROADMAP M2 perf bar) with general mechanisms only: no title-specific code, no weakened correctness.

## Acceptance Criteria

- [x] Design note and slice plan in `docs/spec/gpu-driver.md`.
- [x] Slice 1: `BytesEqual` replaces the TextureCache memcmp and the `GuestBufferMemory` snapshot comparisons in `AddSnapshot` and `Upload`, with a GoogleTest that fails when the helper is broken.
- [ ] Baseline and after numbers on the release preset (average, 1% low, per-stage metrics) from a maintainer machine. Not measurable in the cloud container used for slice 1.
- [ ] Slice 2: cheaper texture revalidation (spec open question 10).
- [ ] Slice 3: reuse prepared `ShaderResources` state across unchanged draws.
- [ ] Slice 4: batch guest reads and uploads.

## Out of Scope

- Render-target-as-texture copy (`RenderTexture.cpp`), owned by another session.
- Recorder wiring (`portps5-tiod`) and the shader recompiler.

## Summary of Changes

Slice 1 only so far: `Graphics/include/BytesEqual.hpp`, `Graphics/src/TextureCache.cpp`, `Graphics/src/GuestBufferMemory.cpp`, `tests/BytesEqualTests.cpp`, `CMakeLists.txt`, spec note. This bean stays open for slices 2 to 4.
