---
# portps5-r7qk
title: 'perf(agc): make sampling a resident render target cheap'
status: todo
type: task
priority: normal
created_at: 2026-10-01T01:00:00Z
updated_at: 2026-10-01T01:00:00Z
---

## Context

`TextureCache::Get` never hits its cache for a render-target source, because `ResidentColor::Begin` bumps the generation on every draw pass. Each read after a write constructs `Texture(context, source, ...)` (Graphics/src/RenderTexture.cpp): new `VkImage`, device allocation, view, `DrawQueue::Flush()`, a `vkCmdCopyImage` batch and its own submit. Measured on one gate title (release preset, metrics only): srcHit=0, about 240 creations per 400 lookups at ~0.25 ms each in one scene, 50-75 in a heavy scene, and the flush/submit adds to Graphics.DrawQueue.Wait / Graphics.Wait (~6-7 ms/frame in heavy frames).

## Higher Goal

Cheap render-to-texture reads without weakening correctness: content equals the target at the draw, guest/write-back changes are seen, unsupported cases log and fail.

## Acceptance Criteria

- [x] Design note in docs/spec/gpu-driver.md (Target design, Open question 10)
- [ ] Baseline on the `release` preset (avg and 1% low frame time, creations per 400 lookups, Graphics.Wait / DrawQueue.Wait totals), recorded as results JSON via compat-result (needs a local Windows machine and the title dump)
- [ ] Step 1: pooled destination images keyed by extent and format
- [ ] GoogleTest regression test over the pool/cache logic with no game data
- [ ] After measurement: decide step 2 (no Flush) and update the spec
- [ ] Re-measure with identical build flags and run protocol; report before/after
- [ ] gpu-driver.md and ROADMAP checkboxes ticked

## Out of Scope

General per-draw prepare cost and texture memcmp revalidation (another session), shader recompiler, title-specific paths.

## Summary of Changes

Design note and bean only so far. No code change: the authoring environment was a Linux cloud container without MinGW-w64, a GPU or the title dump, so nothing could be built or measured.
