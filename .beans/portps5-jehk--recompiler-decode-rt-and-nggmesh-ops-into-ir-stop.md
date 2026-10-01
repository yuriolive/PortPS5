---
# portps5-jehk
title: 'Recompiler: decode RT and NGG/mesh ops into IR, stop at Unsupported()'
status: todo
type: task
priority: normal
created_at: 2026-10-01T18:12:42Z
updated_at: 2026-10-01T18:15:26Z
parent: portps5-epoi
---

## Context

2.0 titles use ray-tracing (image_bvh_intersect_ray and friends) and primitive/mesh stages. If the decoder rejects them outright, 2.0 changes the IR enum, the decoder and the cache key together. Blocked by: none.

## Higher Goal

RT and NGG/mesh ops are decodable IR from 1.0 on; 2.0 only adds their lowering.

## Acceptance Criteria

- [ ] Decoder recognises the RDNA2 BVH intersection opcodes and NGG/primitive-export forms into IR ops (public RDNA2 ISA only)
- [ ] Lowering stops at Unsupported() with a logged op name, never a silent skip
- [ ] Synthetic golden tests: decode succeeds, lowering aborts (EXPECT_DEATH)
- [ ] shader-recompiler.md updated

## Out of Scope

SPIR-V lowering of these ops (2.0 M9).

## Summary of Changes

TBD
