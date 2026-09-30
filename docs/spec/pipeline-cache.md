# PortPS5 — Spec: Pipeline cache

Status: draft v1 · 2026-09-27

## Scope

This spec covers persisting recompiled shaders and Vulkan pipelines across runs: cache keys and variants, the on-disk format and invalidation, warm-up, and the telemetry that proves PRD F7 (a warm cache logs 0 shader compilations and 0 pipeline creations during a regression pass).

Translation itself is in [shader-recompiler.md](shader-recompiler.md). Pipeline construction is in [gpu-driver.md](gpu-driver.md).

## Current state

Neither tree persists anything. Every structure below lives in process memory only. File references are to AnyPS5 `main@75a8668` (includes merged PR #5) unless marked `main@e06dbff` (pre-merge main).

| Layer | What exists | Where |
|---|---|---|
| Source key | `RecompileCacheKey::Build` covers the stage, the code (`main@75a8668`: size plus a 64-bit `HashCode`, verified word by word, see `Recompiler.cpp:232-234`; `main@e06dbff`: every code word), wave size, user-data base and count, the compute/pixel/vertex stage info, `SpirvTarget` (Vulkan/SPIR-V version, subgroup size, `bdaAbiVersion`, capabilities, extensions, limits), and `DebugProbeActive()`. | `core/shader/recompiler/CacheKey.hpp:13-35,173-187` |
| Source entry | `SourceEntry` holds the code, the `IrResourcePlan`, a `planFailure` memo, and the variants. Entries sit in a static `unordered_map` keyed by the source key, with buckets for hash collisions. | `Recompiler.cpp:180-195,222-274` |
| Variant | `CompiledVariant` holds a `ResourceSpecialization`, the `BindingLayout` and the SPIR-V. Lookup is a **linear scan** over `sameLayout` and specialisation equality. `variantId` is a **process-local atomic counter**. | `Recompiler.cpp:163-169,304,360-382` |
| Result memo | 256 entries per source, LRU, keyed by `variantId ^ snapshotHash` (the descriptor words, the flattened SRT, the user data, the uniform fill, the V# fields). | `Recompiler.cpp:409,439-480,484-547` |
| Graphics pipelines | `CachedPipeline` builds a byte key from the device handle, per-stage `variantId`, vertex input, `LayoutKey`, blend/raster state, and mesh or tessellation configuration. The cache is LRU with 256 entries. | `libSceAgcDriver/Graphics/src/Pipeline.cpp:282-347,414-462` |
| Compute pipelines | Keyed by `(variantId << 1) \| push`. | `libSceAgcDriver/Execution/src/VulkanDevice.cpp:2925,3232` |
| `VkPipelineCache` | Created **with no initial data** and never serialised. Every pipeline is created through it. | `libSceAgcDriver/Graphics/include/PipelineCache.hpp:10-13`; `Pipeline.cpp:162` |
| `main@e06dbff` graphics | Keyed on the full SPIR-V bytes, 128 entries. | `main@e06dbff` `Graphics/src/GraphicsPipelineCache.cpp:90-98,129` |
| Request format | `RequestSerializer` produces base64 with magic `0x41505335` and version 2. It **includes the captured guest memory regions**. | `core/shader/recompiler/ControlFlow/src/RequestSerializer.cpp:478,659-693` |
| Replay | `agc_shader_replay` replays the `shader_<addr>.req` files that `dumpRequest` wrote when `APS5_DUMP_SHADERS` was set. | `libSceAgcDriver/tools/ShaderReplay.cpp:98-120`; `Driver.cpp:1663` |

Nothing reads `pipelineCacheUUID`, `driverUUID` or `deviceUUID`. Because `variantId` is process-local, today's pipeline keys cannot be persisted.

## Decision

As agreed in the decision table in [README.md](README.md#subsystem-specs) §Pipeline cache (1.0 scope), there is one on-disk cache per title. It is keyed by the recompiler version, the request hash and the driver UUID, and holds SPIR-V, a `VkPipelineCache` blob and variant metadata. It is invalidated when the recompiler version or the device changes. On top of that decision, this spec replaces `variantId` in every persisted key with a content hash, uses 128-bit hashes on disk, and adds a warm-up phase, so that "0 creations" is measured after warm-up, as `pipeline_creations_after_warmup` in [verification.md](verification.md) §4.

## Target design

**Location.** `%LOCALAPPDATA%/PortPS5/cache/<titleId>/` (proposal), a sibling of the saves directory. It stays local and is never uploaded, because it holds game-derived SPIR-V.

**Keys.** This spec is the only definition of the cache keys and of `RecompilerVersion`; other specs link here. All hashes are 128-bit XXH3 over a canonical little-endian encoding.

| Key | Inputs |
|---|---|
| `EnvKey` (file header) | Cache format version, `RecompilerVersion`, `BdaAbi::Version`, `driverUUID`, `pipelineCacheUUID`, vendorID, deviceID, `driverVersion`. |
| `SourceKey` | The `RecompileCacheKey` fields without `DebugProbeActive()`, with a 128-bit code hash in place of `HashCode`. |
| `VariantKey` | `SourceKey`, `ResourceSpecialization`, and the `BindingLayout` fields that `sameLayout` compares. |
| `SpirvHash` | The emitted SPIR-V words. They identify the variant in every pipeline key. |
| `PipelineKey` | AnyPS5 main's (merged PR #5) `pipelineKey` fields, with `SpirvHash` per stage in place of `variantId` and without `context.device`. Compute: `SpirvHash`, the push flag and the set layout. |

`RecompilerVersion` combines a hash of the recompiler's sources, computed by CMake at build time (see [build-toolchain.md](build-toolchain.md)), with `kCacheEpoch`, a manually bumped schema number for format or semantic changes the source hash cannot see.

**In-memory variant index.** `SourceEntry::variants` becomes a map keyed by the same `VariantKey`, with an LRU bound, which delivers the "bounded hash-indexed variants" item ([shader-recompiler.md](shader-recompiler.md) §Target design 3).

**Failures are never cached.** A recompile or plan failure logs once with the program hash and aborts via `Unsupported()`, in every mode. AnyPS5 main's (merged PR #5) `planFailure` memo is not ported and no failure record exists on disk. The result memo is unchanged, because it depends on per-frame snapshot values that are never persisted.

**On-disk layout.** Each file is append-only, and each record is framed as `{u32 tag, u32 length, u128 key, payload, u64 xxh3}`.

| File | Records |
|---|---|
| `header.bin` | Magic `PPS5CACH` and the `EnvKey`. |
| `plans.bin` | `SourceKey` mapped to the serialised `IrResourcePlan`, so that a warm run skips `PrepareResourceProgram`. |
| `variants.bin` | `VariantKey` mapped to the SPIR-V, `CompiledShaderInfo`, binding allocation (layout only), `bdaAbiVersion`, vertex/instance offset SGPRs, parameter exports and fragment parameters. This covers every `RecompileResult` field except the per-snapshot bindings and push constants. |
| `pipelines.bin` | `PipelineKey` mapped to the full pipeline description needed to re-create it without a draw. |
| `vk.bin` | The `vkGetPipelineCacheData` blob. |

**Load.** Compare `header.bin` against the live `EnvKey`. On a mismatch, rename the directory to `*.stale` (only the last stale generation is kept) and start empty. Validate `vk.bin` against `VkPipelineCacheHeaderVersionOne` (`headerSize`, version, vendorID, deviceID, `pipelineCacheUUID`) before passing it as `pInitialData`. Then stream the records; a bad checksum or a truncated tail ends that file's load at the last good record.

**Warm-up.** Before the first guest submission, a small pool of workers re-creates every pipeline in `pipelines.bin` through the loaded `VkPipelineCache`, and blocks the title's first flip until they finish. Plans and variants load lazily on first lookup.

**Write-out.** New records are appended as they are created, so a crash loses at most the unflushed tail. `vk.bin` is rewritten atomically (temp file plus `MoveFileEx` replace) at shutdown, and every 60 s while new pipelines are appearing; per-thread caches are merged with `vkMergePipelineCaches`. A per-title lock file prevents two processes from writing. A size cap is enforced by compaction at startup (proposal: 2 GiB): records not hit in the last N runs are dropped.

**Telemetry**, exported per run to the results JSON:

| Counter | Meaning |
|---|---|
| `plan_builds` | Front-end runs (`PrepareResourceProgram`). |
| `spirv_compilations` | `compileVariant` calls, which F7 counts as shader compilations. |
| `pipeline_creations` | `vkCreate*Pipelines` calls after warm-up. It is reported as `pipeline_creations_after_warmup`. |
| `warmup_ms`, `cache_bytes`, `rejected_records` | Warm-up cost and cache health. |

## Interfaces

| Peer | Contract |
|---|---|
| [shader-recompiler.md](shader-recompiler.md) | Builds `SourceKey` and `VariantKey` exactly as defined here, and exposes serialisers for `IrResourcePlan` and `CompiledVariant`. Structurizer or emitter changes change `RecompilerVersion` through the source hash. |
| [gpu-driver.md](gpu-driver.md) | The `PipelineCache` module takes a `PipelineDesc` built from `SpirvHash` values, never from `variantId` or handles, and owns the `VkPipelineCache`. |
| [configuration.md](configuration.md) | `debug.pipeline_cache` (enum `on` \| `off` \| `readonly`) and `debug.pipeline_cache_verify` (bool: recompile on a hit and compare the SPIR-V). Both are diagnostic only, and no release run may set them. There is no `[workarounds]` key. |
| [save-data.md](save-data.md) | Shares the `%LOCALAPPDATA%/PortPS5/` root. Deleting the cache never touches saves. |
| [build-toolchain.md](build-toolchain.md) | Generates `RecompilerVersion` from the recompiler's source list. |
| [verification.md](verification.md) | Warm-cache runs report `pipeline_cache: "warm"` and the counters above. |

## Failure modes

| Condition | Behaviour |
|---|---|
| `EnvKey` mismatch (new driver, GPU or recompiler). | Full invalidation. A cold run follows, and the results JSON reports `pipeline_cache: "cold"`. |
| Corrupt or truncated record. | That record and every later one in the file are dropped, and a warning is logged. |
| The driver rejects the `vk.bin` blob. | Retry with no initial data. Pipelines are still created from `pipelines.bin`, only more slowly. |
| A 128-bit key collision (negligible). | Caught by `debug.pipeline_cache_verify`, which is on in local regression. |
| Recompile or plan failure. | Never written to the cache. The recompiler logs once with the program hash and aborts via `Unsupported()`. |
| Disk full or lock held. | Readonly mode, with a warning. |
| A key field is missing from `VariantKey`. | A stale variant is served. Golden tests catch this, and the key is derived from the same struct the request serialiser walks. |

## Tests

| Layer | Tests |
|---|---|
| unit (hosted) | Key stability across processes (the same serialised request gives the same `VariantKey` and `SpirvHash`); each `SpirvTarget` field changes the key; record framing round-trip; truncation and bad-checksum recovery; `EnvKey` mismatch invalidates; the variant index bound. |
| `recompiler-golden` (hosted) | Replay the synthetic corpus twice into a temporary cache. The second pass has `plan_builds == 0` and `spirv_compilations == 0`, and its SPIR-V is byte-identical to the goldens. |
| `driver-lavapipe` (hosted) | `vk.bin` round-trip with header validation. Run a synthetic draw and dispatch, restart, warm up, and assert `pipeline_creations == 0`. |
| local regression | Every gate title runs cold, then warm. The warm pass must show 0 `spirv_compilations` and 0 `pipeline_creations_after_warmup`. `warmup_ms` is recorded. |

## Milestones

| Milestone | Delivers |
|---|---|
| M1 | - [x] Prerequisites: request serialisation and `agc_shader_replay` are ported, and the `recompiler-golden` job gives the corpus the cache tests replay. |
| M2 | - [ ] The disk pipeline cache (F7): the key rework, all five files, warm-up, telemetry, and the lavapipe and golden tests. |
| M3 | - [ ] Bounded, hash-indexed in-memory variants. Structurizer fallback changes bump `RecompilerVersion`. |
| M4 | - [ ] Subgroup-size-control variants and descriptor-heap layouts enter the keys. |
| M5 | - [ ] Tuning of the size cap and warm-up time during the performance pass. |
| M6 | - [ ] The release full runs use a warm cache ([verification.md](verification.md) §3). |

## Open questions

1. Is persisting `IrResourcePlan` cheaper to build and maintain than re-running the front end with only `compileVariant` cached? F7's "0 shader compilations" is defined here as `spirv_compilations`.
2. Is warm-up before the first flip acceptable for titles with more than 10k pipelines, or should warm-up be ordered by first use and overlapped with boot?
3. Could `VK_EXT_graphics_pipeline_library` or `VK_EXT_shader_module_identifier` shrink the cache and the warm-up time? Both are optional, and deferred until M5 data exists.
4. What should the compaction policy be ("not hit in N runs")? It needs a run counter in `header.bin`.
