# PortPS5 — Spec: Shader recompiler

Status: draft v1 · 2026-09-27

Deepens the decision table in [README.md](README.md#subsystem-specs). The decisions recorded there are fixed: keep the pipeline, replace the structurizer's failure path, add a subgroup-size path and tests, and leave AOT post-1.0. Paths are relative to AnyPS5 `core/shader/recompiler/` unless prefixed. `main@e06dbff` is the pre-merge AnyPS5 main (the old baseline). `main@75a8668` is current AnyPS5 main, which includes merged PR #5. Its delta column cites `main@75a8668`. Anything marked *(inferred)* was not observed at runtime.

## Scope

Everything from a `RecompileRequest` (RDNA2 code, guest register context, a memory view, a host target) to validated SPIR-V plus binding metadata:

- RdnaDecoder, CFG build and Structurizer;
- InstructionTranslator, IR (IrOpcode, IrBlock, SSA builder, `ValidateProgram`) and the optimization passes;
- SrtWalker and resource planning (`IrResourcePlan`, `pureFlatSlots`, ResourceMaterializer);
- the SpirvBackend (SpirvEmitter, SpirvModule, SpirvMemory, SpirvSubgroup, SpirvBda*);
- the SPIRV-Tools optimizer, source and variant caching, RequestSerializer, and the `agc_shader_replay` tool.

Out of scope: PM4 parsing, descriptor upload and pipeline creation ([gpu-driver.md](gpu-driver.md)), and on-disk persistence ([pipeline-cache.md](pipeline-cache.md)).

## Current state

`core/shader` is about 33.8k lines across 206 files at `main@e06dbff` and 35,950 lines across 208 files at `main@75a8668` (`git ls-files`, summed line count). PR #5 branched from `40df528`, and main made no commits under `core/shader` between `40df528` and `e06dbff`. PR #5 changed the recompiler in exactly two commits, both now ancestors of `main@75a8668`: `87911b3` (+977/−112, 38 files) and `29b4601` (+1343/−259 under `core/shader`). Sixteen later commits touch `core/shader` (translator fixes and additions from other contributors, for example `S_CLAUSE`, `V_PERM_B32` and `s_getpc_b64`). `core/shader` contains 0 `getenv` calls at `main@e06dbff` and 33 at `main@75a8668`.

| # | Stage | `main@e06dbff` (file:line) | `main@75a8668` delta (merged PR #5) | Target |
|---|---|---|---|---|
| 1 | Decode | `RdnaInstructionDecoder` (`Recompiler.cpp:66-67`). About 560 `RdnaOpcode` entries (grep count). An unknown opcode throws at translation (`Translation/src/DispatchInstructions.cpp:19`). | SMEM with a VCC base; f16 conversion opcode fix (`87911b3`) | Keep. Add per-opcode coverage counters for the corpus. |
| 2 | CFG | `GraphBuilder` (`Recompiler.cpp:69-70`) | none | Keep. |
| 3 | Structurize | `Structurizer::Structurize` (`ControlFlow/src/Structurizer.cpp:581-627`). It has 11 `throw` sites (lines 476, 593, 600, 603, 621, 819, 829, 843, 858, 862, 875). Irreducible control flow is flagged at :765 and rejected at :875. Block cloning is disabled (:476). | identical file | Add a goto-elimination fallback (§Target design 1). |
| 4 | Translate | `InstructionTranslator` (`Recompiler.cpp:93`), then `ValidateProgram(program, false)` (`Translation/src/InstructionTranslator.cpp:438`) | saveexec order (`Translation/src/ControlFlowInstructions.cpp:18-31`), `v_movrels`/`v_movreld` lowering (:243-270) | Port both fixes and remove their env switches. |
| 5 | SSA | Braun-style construction: sealing in `IrBlock.hpp:32-45`, `TryRemoveTrivialPhi` at `Optimization/src/SsaBuilder/SsaPass.cpp:103` | none | Keep. Add synthetic flag variables (§1). |
| 6 | Cleanup | `ConstantFolder`, `ResolveControlFlowIdentities`, `DeadCodeEliminator`, `ReadLaneEliminator` (`Recompiler.cpp:98-113`). `SharedMemoryBarrierInserter` is a 10-line stub that nothing calls. | ReadLaneEliminator +92 lines | Keep. Delete the stub. |
| 7 | SRT and resource tracking | `SrtWalker::BuildPlan` and `ResourceTracker::Track` (`Recompiler.cpp:115-121`) | flat-slot classes (`SrtFlatSlotClasses.cpp`, new); `pureFlatSlots` (`IrMetadata/ResourcePlan.hpp:66`) | Port. |
| 8 | Plan and materialize | `ResourceMaterializer::ExtractPlan` and `Materialize` (`Recompiler.cpp:137,287`) | bindless image tables with fixed caps: `APS5_BINDLESS_SLOTS`, default 16 (`ResourceMaterializer.cpp:855-862`), and `APS5_BINDLESS_MATERIAL_SCAN`, default 256 (:183-191) | Derive the bounds from device limits (§4). |
| 9 | Source and variant cache | The source key is every code word plus the context and target (`CacheKey.hpp:12-23`). Variants are found by **linear scan** and never bounded, and the compile runs **under the source mutex** (`Recompiler.cpp:290-303`). Nothing persists. | The code enters the key as length plus a 64-bit hash and is verified word by word (`CacheKey.hpp:16-25`, `Recompiler.cpp:222-273`). A result memo holds 256 LRU entries (:409). There is a plan-failure memo (:190,263-268). Variants are still a linear scan (:494-504). | Bounded, hash-indexed lookup that compiles outside the lock (§3). |
| 10 | Bind | `ShaderInfoCollector`, `BindingAllocator`, `DescriptorBindingBuilder` (`Recompiler.cpp:203-210`) | written/atomic flags on `DescriptorBinding` | Port. |
| 11 | Emit | `SpirvEmitter::Emit` (`SpirvBackend/src/SpirvEmitter.cpp:205-222`). `laneCount = 2` when a wave64 program meets a 32-wide host (:217). On `main@e06dbff`, `hostSubgroupSize` keeps its default of 64 (`ShaderStageInputInfo.hpp:92`) and is never assigned, so the two-lane path is unreachable *(inferred from grep)*. | `HostSubgroupSize(request)` plumbs `target.subgroupSize` through (`Recompiler.cpp:72-82`). `WaveLdsScope` wave-LDS ordering barriers (`SpirvEmitter.cpp:41-67`). Atomic-zero skip (`SpirvMemory/SpirvMemoryInstructions.cpp:730-751`). | Port. Add a subgroup-size-control path (§2). |
| 12 | Validate and optimize | `ValidateAndOptimizeSpirv` (`SpirvBackend/src/SpirvOptimizer.cpp:9-57`) runs validate, then `RegisterPerformancePasses`, then validate again. It is compiled only with `ANYPS5_ENABLE_SPIRV_TOOLS`, which defaults to OFF (`CMakeLists.txt:164-168`). | per-function pass list | ON in the `dev` and `ci` presets. The release default is currently off, pending PRD R1. |

Other facts:

- **Driver target.** The driver builds its target as Vulkan 1.3 with SPIR-V 1.3, or 1.4 when mesh shaders are used (`libs/prx/libSceAgcDriver/Execution/src/VulkanDevice.cpp`, `VulkanDevice::Target`). It stays below 1.6 until the subgroup size is pinned per pipeline ([gpu-driver.md](gpu-driver.md) Open questions), because the lane math assumes a fixed `target.subgroupSize`. `ValidateAndOptimizeSpirv` already maps Vulkan 1.3 and 1.4 to a SPIR-V 1.6 ceiling and rejects a 1.6 target under a 1.1 or 1.2 environment.
- **Serialization.** `RequestSerializer` writes magic `0x41505335`, version 2, as base64 (`ControlFlow/src/RequestSerializer.cpp:659-693`). `Recompile` appends the serialized request to every exception (`Recompiler.cpp:318-327`).
- **Replay tool.** `agc_shader_replay` (`core/libs/prx/libSceAgcDriver/tools/AgcShaderReplay.cpp`,
  `agc_shader_replay` CMake target, dev/ci presets only, never shipped) replays serialized
  `.req` requests through `Recompile`, prints RDNA disassembly (`--dis`), SPIR-V assembly
  (`--asm`) and the snapshot summary (`--mem`), and verifies a corpus directory
  (`--golden <dir>`, with deliberate regeneration via `--update-goldens`). It carries no
  `APS5_*` environment switches (explicit flags only) and returns codes, never throwing
  across boundaries. `Deserialize.cpp` remains an unbuilt standalone diagnostic.
- **Tests.** Recompiler-owned test targets (both in ctest):
  - `recompiler_fixes_tests` (label `unit`): saveexec order, atomic-zero, `v_movrels`/`v_movreld`, wave-LDS barriers;
  - `recompiler_golden_tests` (label `golden`, `ctest --preset golden`): the synthetic corpus
    coverage gate plus wave32/wave64 replay of every case (see Tests below).
  The driver tests that touch the recompiler are:
  - `agc_shader_memory_tests`, which is in ctest and checks the serializer round-trip and the cache policy;
  - `agc_driver_recompiler_tests`, which is built but not registered.
- **glslang.** `tests/DummyShaders.cpp` is compiled into the recompiler static library and pulls glslang in as a link dependency (`CMakeLists.txt:136,158-162`). Nothing references it outside the file, so the static archive probably drops it *(inferred)*.

## Decision

- **Keep** the pipeline shape.
- **Adopt from AnyPS5 main (merged PR #5):** all of `87911b3`, plus the flat-slot classes and hashed source key from `29b4601`. Every `APS5_*` read is removed or moved into the typed `[debug]` config.
- **Reject:**
  - the plan-failure memo, in any form: a recompile or plan failure logs once with the program hash and aborts via `Unsupported()`, with no skip, memo or counted tolerance in any mode;
  - the fixed bindless caps;
  - title identifiers in comments (for example `Recompiler.cpp:189`).
- **Add:**
  - a structurizer fallback that always succeeds;
  - `VK_EXT_subgroup_size_control`;
  - a bounded, hash-indexed variant index;
  - the stable `SourceKey` and `VariantKey` defined in [pipeline-cache.md](pipeline-cache.md#target-design);
  - a kernel-idiom analysis for the driver (`KernelIdiom`);
  - the driver-side data paths: V#/SRT loads on the GPU through BDA, SGPRs from a user-data buffer, and the descriptor-heap hash probe;
  - the synthetic and local corpora.

## Target design

**1. Structurizer fallback.**

- `Structurize` becomes `StructurizeResult Structurize(ControlFlowGraph&)`. Tier 1 is the existing algorithm, with every `throw` turned into a returned `Unstructurable{reason, block}`.
- On failure, tier 2 runs over the **original** CFG, a copy taken before tier 1 mutates it. Tier 2 is goto elimination in the Erosa–Hendren style (the 1994 "Taming Control Flow" paper). Yuzu and shadPS4 use the same family *(external, not checked here)*.
- Tier 2 algorithm:
  1. Build a statement tree in which every CFG edge that is not a structured fall-through becomes a `goto L`.
  2. Move each goto outward or inward until it is a sibling of its label, rewriting as it goes: `if (c) goto L` becomes `flag_L = c; if (!flag_L) {…}`, and a goto that crosses a loop becomes `flag_L = c; break`.
  3. Replace the sibling goto with a conditional over the statements in between. For a backward goto, wrap them in `do {…} while (flag_L)`.
  4. Irreducible regions need no special case, because flags break every multi-entry cycle.
- Flags are new SSA variables with a synthetic slot class `SsaVariable::Kind::Flag`, next to the SGPR/VGPR slots in `IrBlock`. The SSA builder resolves them like registers, and the translator emits `SetFlag` and `GetFlag` IR ops.
- Tier 2 must terminate: it performs O(gotos × depth) rewrites. The emitted tree is structured by construction, and tier 1's merge annotation then runs over it without a failure path. If a budget assert fires in tier 2, that is a recompiler bug. A backstop dispatcher loop (`loop { switch (state) {…} }`) is kept only for fuzz triage.
- Divergence: a flag can be lane-varying, so its uses count as divergent control. `WaveLdsScope` barriers are never placed inside a flag-guarded region. Such a region falls back to workgroup-scope barriers only where the whole workgroup is known to be converged; otherwise it is logged.

**2. Wave size.**

- `SpirvTarget` gains `subgroupSizeControl{minSize, maxSize, requiredStages, fullSubgroups}`, filled from `VkPhysicalDeviceSubgroupSizeControlProperties` (a core feature in Vulkan 1.3, which the reference tier guarantees).
- Policy per program:
  - When `waveSize` lies within [min, max] and the stage is in `requiredStages`, emit `laneCount = 1` with subgroup scope, and return `RecompileResult.requiredSubgroupSize = waveSize`. The driver chains `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo`, plus `REQUIRE_FULL_SUBGROUPS` for compute when the local X size is a multiple of `waveSize`.
  - Otherwise, a wave64 program on a 32-wide host keeps the two-lane emulation, and `requiredSubgroupSize = 0`.
  - A wave32 program on a host with a fixed width above 32 masks the ballot's high bits *(open question: whether any tier device needs this)*.
- The chosen mode enters the cache key through `target`.

**3. Variant index.**

- `SourceEntry` keeps `code` (for collision checks) and `plan`, and holds an in-tree open-addressing map of `VariantKey → Slot`.
- `VariantKey` is the 128-bit key defined in [pipeline-cache.md](pipeline-cache.md#target-design) (`SourceKey` plus layout and specialisation). The in-memory index uses the same key as the disk cache. Equality compares the full values, so the hash only indexes.
- `Slot` holds `shared_future<CompiledVariant>`. The first requester compiles **outside** the source mutex, and concurrent requesters wait on the future.
- Bounds:
  - per-source LRU cap `kMaxVariantsPerSource = 64`;
  - a global byte budget of 256 MiB of SPIR-V plus metadata.
- Eviction drops the entry from memory only. The disk cache can restore it. Evictions and cap hits feed the telemetry counters `shader.variant_evictions` and `shader.variant_cap_hits`.
- The result memo (256 LRU) is kept, keyed the same way.
- Disk keys and `RecompilerVersion` are defined only in [pipeline-cache.md](pipeline-cache.md#target-design). The recompiler builds `SourceKey` from its `RecompileCacheKey` fields and `VariantKey` from `SourceKey`, `BindingLayout` and `ResourceSpecialization`, as specified there, and defines no key of its own.
- `variantId` stays process-local and never enters SPIR-V, so output is deterministic.

**4. Bindless.**

- M1 ports runtime-indexed descriptor arrays with bounds from device limits. This is a bounds change only: the table size comes from `maxPerStageDescriptorUpdateAfterBindSampledImages` and the table's V# record count, not from a constant, and the `APS5_BINDLESS_*` caps go away.
- In M4 the table becomes an index into the driver's GPU descriptor heap (`VK_EXT_descriptor_indexing`). The shader probes the driver's T#-address-to-slot hash table (§7), which replaces the CPU material scan, and the recompiler emits `NonUniform` decorations whenever `tableIndexNonUniform` (AnyPS5 `main@75a8668` `SpirvEmitter.cpp:257`) holds.

**5. Kernel idioms.**

- `std::optional<KernelIdiom> AnalyzeKernelIdiom(const IrProgram&)` runs on the post-SSA, post-SRT program for compute stages and is returned in `RecompileResult.idiom`. The recompiler owns `KernelIdiom`.
- Recognised forms:
  - `UniformFill{dstBinding, strideBytes, value: UserDataSlots[4], countSource}`;
  - `LinearCopy{srcBinding, dstBinding, elemBytes, countSource, srcModulo?}`.
- Proof obligations:
  - no LDS, atomics or loads other than those in the idiom;
  - exactly one store per lane;
  - the address is `base + f(globalInvocationIndex) × stride`, with `f` affine;
  - the value is uniform, or a single load at the same affine index;
  - every guard is `index < count`.
- Recognition is structural, on IR, never on bytes or hashes. The driver owns replacement and verify mode.

**6. Debug surface.**

- AnyPS5 main's switches (`APS5_DUMP_IR`, `APS5_SINGLE_LANE`, `APS5_LOOP_GUARD`, `APS5_PROFILE_DRAW`, …) become the [configuration.md](configuration.md) keys `debug.recompiler.dump_ir` (`["<codeaddr>"|"all"]`), `debug.recompiler.single_lane` and `debug.recompiler.profile`. Request capture is `debug.recompiler.capture`.
- These keys are diagnostic only. No release run may set them.
- Every `APS5_NO_*` / `APS5_*_WRITE_FIRST` switch is deleted once its fix passes the corpus.
- The `v_movrels` select chain costs O(VGPR limit) per access. When M0 is not constant after folding, lower it to a `Function`-storage VGPR array with `OpAccessChain` instead.

**7. Driver-side data paths.** The recompiler must emit code for three driver contracts ([gpu-driver.md](gpu-driver.md)):

- **V#/SRT loads through BDA (M3).** The shader loads the V# and the SRT chain from guest memory at execute time through the BDA page table, using the `bdaAbiVersion` ABI, instead of the driver reading them on the CPU at record time. Out-of-range pointers resolve to the driver's fault buffer.
- **SGPR user data from a buffer (M3).** SGPRs that the CP's patch does not fold (for example indirect-draw fields) are read from a per-draw user-data buffer instead of push constants.
- **Descriptor-heap hash probe (M4).** For bindless tables, the shader probes the driver's GPU hash table from T# address to heap slot (§4).

## Interfaces

| With | Contract |
|---|---|
| [gpu-driver.md](gpu-driver.md) | `ResolveSource`, `CaptureResources` and `Recompile(request, capture)` (AnyPS5 main API, from merged PR #5) stay the entry points. `RecompileResult` gains `requiredSubgroupSize` and `idiom` (`KernelIdiom`). A recompile or plan failure aborts via `Unsupported()`, so there is no failure status for the driver to handle. Today the recompiler reads guest memory only through `RequestMemoryView` captures; the target adds the GPU-side paths of §Target design 7 (BDA V#/SRT loads, the user-data buffer, the heap probe). The driver creates pipelines and applies idiom replacement. |
| [pipeline-cache.md](pipeline-cache.md) | Owns `RecompilerVersion`, `SourceKey` and `VariantKey`. The recompiler builds those keys as specified there and exports `SerializeVariant` and `DeserializeVariant` (SPIR-V, bindings, stage metadata). The cache is authoritative across runs, and in-memory eviction relies on it. |
| [verification.md](verification.md) | The `recompiler-golden` job: `agc_shader_replay --golden <dir>` diffs **pre-optimizer** SPIR-V disassembly and runs `spirv-val` on both pre- and post-optimizer modules. Local regression step 4 replays the local corpus. The `policy` job bans `getenv` in `core/shader`. |
| [build-toolchain.md](build-toolchain.md) | `ANYPS5_ENABLE_SPIRV_TOOLS` is ON in the `dev` and `ci` presets. The release default is currently off, pending PRD R1. `DummyShaders.cpp` moves into a test target, so glslang is test-only. The `golden` ctest label and `ctest --preset golden` entry point. The synthetic corpus is hand-assembled dwords (field layouts cited to the decoder sources in `tests/golden/SyntheticCorpus.hpp`), checked in as `.req` plus `.spvasm` under `tests/golden/corpus/`; no `.s` sources and no `llvm-mc` pin (the hand-assembled form removes the build-time tool entirely). `.req` files are generated with `agc_shader_replay --dump-corpus`, goldens with `--golden <dir> --update-goldens`. |
| [configuration.md](configuration.md) / telemetry | `debug.recompiler.dump_ir`, `debug.recompiler.single_lane`, `debug.recompiler.profile`, `debug.recompiler.capture` (diagnostic only). Counters: `spirv_compilations` (also the results JSON field), `shader.compile_ms`, `shader.structurizer_tier2`, `shader.variant_evictions`, `shader.variant_cap_hits`. `spirv_compilations` feeds F7's "0 compilations with a warm cache". |

## Failure modes

| Failure | Detection | Behaviour |
|---|---|---|
| Tier 1 structurizer cannot place a merge | `Unstructurable` result | Tier 2. Counted, never skipped. |
| Tier 2 budget assert | fuzz or corpus | Recompiler bug: dump the `.req` and fail the test. At runtime, log once with the program hash and abort via `Unsupported()` (see the next row). |
| Unsupported opcode or unresolvable SRT chain (any recompile or plan failure) | exception carrying the serialized request | Log once with the program hash and abort via `Unsupported()`. No skip, no failure memo and no counted tolerance, in any mode. |
| Source-key hash collision | word-by-word code compare (`main@75a8668`) | A second bucket entry, which is correct. Counted. |
| Variant explosion (for example descriptor churn) | `variant_cap_hits` | LRU eviction and a disk reload. *Open: whether a despecialised variant is feasible.* |
| `spirv-val` or optimizer failure | SPIRV-Tools | A recompile failure: log once with the program hash and abort via `Unsupported()`. In a build without SPIRV-Tools (the current release default), the driver's capability check (`Graphics/src/ShaderValidation.cpp`) is the last guard. |
| Subgroup size not honoured by the driver | pipeline creation fails, or a debug-mode ballot self-check | Fall back to `laneCount = 2` for that stage and log once. |
| Golden drift from a SPIRV-Tools bump | golden diff on post-opt output | Not possible: goldens are taken before the optimizer. |

## Tests

- **GoogleTest Unit Suites** (`recompiler_tests`, hosted `unit` job):
  - [ ] Decoder round-trip per opcode encoding with parameterized tests (`TEST_P`).
  - [ ] `ValidateProgram` negative cases.
  - [ ] SSA with synthetic flags.
  - [ ] Tier 1 and tier 2 structurizer on hand-built CFGs (diamond, multi-latch, shared merge, the two irreducible entries, a goto into a loop).
  - [ ] Variant index: bound, eviction, concurrent single compile.
  - [ ] Key stability: the same request gives the same `SourceKey` and `VariantKey` ([pipeline-cache.md](pipeline-cache.md#target-design)) across processes.
  - [ ] Idiom analysis, positive and negative.
  - [ ] Emission of BDA V#/SRT loads, user-data-buffer SGPR reads and the heap probe (§Target design 7).
  - [x] Regression tests for saveexec `(vcc, vcc)`, atomic-zero, `v_movrels` and wave-LDS scope (`RecompilerFixesTests`),
    including divergent-write barriers at reconvergence merge blocks, divergent-read barriers at
    outermost uniform headers (`DivergentRegionLdsReadOrdersPriorWritesAtUniformHeader`,
    `DivergentWriteThenReadInSameBlockGetsHeaderAndMergeBarriers`), and the cyclic-header skip
    (`DivergentReadWithCyclicHeaderSkipsPreReadBarrier`).
  - [x] `s_getpc_b64` names the shader's absolute address (`GetShaderBase` + next-PC offset, ported
    from upstream `d9ac21c`): PC-relative data behind the shader's own code resolves to the real
    data instead of near-zero, and relocated copies keep sharing one variant
    (`SGetpcB64AddsShaderBaseToNextPc`).
  - [x] Stage gate: `SharedMemoryBarrierInserter` inserts barriers only for compute, mesh, and
    tessellation-control stages (Workgroup execution scope is invalid elsewhere), covered by
    `Wave64VertexStageSkipsBarrierInsertion` and `TessellationControlStageInsertsBarrier`.
  - [ ] Death tests (`EXPECT_DEATH`): verify that unresolvable opcodes trigger an immediate logging abort via `Unsupported()` without memory corruption.
- **Ported Ecosystem Test Suites:**
  - [ ] **KytyPS5 `ShaderRecompilerComputeTests`:** comprehensive RDNA2 instruction lowering, resource descriptor bindings, texture sampling modes, and atomic memory operations.
  - [ ] **KytyPS5 `shaderCfgTests`:** advanced control-flow graphs, loop structuring, loop termination conditions, and complex nested branching topologies.
  - [ ] **Mesa ACO GFX10.3 Test Suite (`src/amd/compiler/tests`):** bitfield-exact RDNA2 instruction decoding, DPP swizzles, SDWA packing, 64-bit LDS instructions, and divergent control-flow reconvergence.
  - [ ] **SharpEMU Packed ALU Suites:** packed 16-bit math (`VopcF16`), 3-input XOR (`ThreeInputXor`), and 24-bit signed multiplication (`SignedMultiply24`).
- [x] **Synthetic golden corpus** (hosted): project-written dwords only, never game bytecode
  (`core/shader/recompiler/tests/golden/`, corpus in `corpus/`, replayed two ways: the
  `recompiler_golden_tests` GTest suite and `agc_shader_replay --golden <dir>`).
  - Coverage gate: every `RdnaOpcode` the decoder accepts appears in at least one request, and CI fails when a decoded class (SOP1/SOP2/SOPK/SOPC/SOPP/SMEM/VOP1/VOP2/VOP3/VOPC/VOP3P/DS/MUBUF/MTBUF/MIMG/FLAT/EXP) has no request.
  - Each variant is replayed with a wave32 and a wave64 target. The with/without subgroup
    size control axis arrives with the M4 subgroup-size-control path (Target design §2);
    until then the corpus pins `subgroupSize` to the wave size.
  - Golden diffs compare the disassembly of the final validated module (the `Recompile`
    contract returns post-optimizer SPIR-V when SPIRV-Tools are on). Goldens therefore pin
    the SPIRV-Tools version; regenerate deliberately with `--update-goldens` after review.
    Comparisons normalize CRLF (Windows checkouts) against the LF-only disassembler output,
    and replaying zero requests fails, so CI can never pass on a wiped corpus.
- **Fuzz** (hosted, fixed seeds, plus a nightly local run): a random CFG generator emits `s_branch` / `s_cbranch_*` programs, including irreducible ones, and mutates corpus CFGs. Oracle: 0 structurizer throws, `spirv-val` passes, and an interpreter over IR matches an interpreter over the RDNA CFG on the branch trace for random SGPR inputs.
- **Local game-derived corpus:** `.req` files captured under `debug.recompiler.capture = true`. They stay on the maintainer machine (verification.md §2), and the pass condition is 0 validation failures and no `Unsupported()` abort.

## Milestones

| Milestone | Recompiler deliverables | Exit evidence |
|---|---|---|
| M0 | - [x] C++23 flag. `recompiler_tests` and `agc_shader_memory_tests` in ctest. glslang made test-only. | ctest green |
| M1 | - [ ] Port `87911b3` and the flat-slot and hashed-key parts of `29b4601`. Bindless tables with bounds taken from device limits. Remove every `APS5_*` read.<br>- [x] `agc_shader_replay` and serializer (adapted port: no env switches, return codes).<br>- [x] The `recompiler-golden` job.<br>- [x] The synthetic corpus. | M1 exit: every decoded class covered and green in CI, local corpus with 0 failures, DeS fill/copy kernels running as compiled shaders |
| M2 | - [ ] `SourceKey`/`VariantKey` as defined in [pipeline-cache.md](pipeline-cache.md#target-design), and variant (de)serialisation for the disk cache. Depth and sample-mask export verified. | 0 `spirv_compilations` with a warm cache (F7) |
| M3 | - [ ] Tier 2 structurizer. Bounded hash-indexed variants that compile outside the lock. V#/SRT loads on the GPU through BDA. SGPRs read from the user-data buffer. | Fuzz corpus with 0 structurizer throws |
| M4 | - [ ] Subgroup size control. `AnalyzeKernelIdiom` (`KernelIdiom`). Bindless on the GPU heap through the descriptor-heap hash probe, replacing the CPU material scan. | Bugsnax full run |
| M5 | - [ ] Optimizer pass-list tuning, measured against `shader.compile_ms` during cold runs | Perf bar |
| M6 | - [ ] R1 status: record whether the release binary contains SPIRV-Tools | Release notes |

**Relink-time AOT stays post-1.0.** Four things block it:

1. Variants specialise on descriptor contents read from guest memory at dispatch time (`Recompiler.cpp:286-287`).
2. Stage context (interpolators, vertex fetch, LDS size, wave size) comes from PM4 register state that exists only at draw time.
3. Shader code can live in data files loaded at runtime rather than in the ELF *(inferred)*.
4. The SPIR-V depends on the target device.

A post-1.0 path pre-warms the disk cache from a recorded `.req` corpus.

## Open questions

1. Can a despecialised generic variant stand in once a source hits the variant cap?
2. Does any wave32 program run on a host whose subgroup is fixed wider than 32?
3. Follow-up to PR #36 (beads portps5-52): `EmitGetShaderBase`
   (`SpirvBackend/src/SpirvModuleEmitter.cpp:1052`) still emits constant zero, so
   SPIR-V consumers of an `s_getpc_b64` value see offset-only while the
   SRT/resource-tracker path (via `MakeRuntime` with `request.shader.codeAddress`)
   names the real base — same limitation as upstream. The fix is dispatch-time
   delivery of the code address (per-dispatch shader data or push constants), never
   baked into the shared variant.
4. Does the M1 intro-cinematic stage require bindless tables? The message of commit `29b4601` says so *(unverified)*.
5. Should tier 2 run eagerly in CI for every corpus shader, to find divergence bugs before games do?
6. Choice of XXH3: vendoring it (BSD-2) versus an in-tree hash.
7. Intra-divergent-region cross-lane LDS ordering: `SharedMemoryBarrierInserter` orders divergent writes
   at reconvergence merges and divergent reads at outermost uniform headers (skipped when the header
   lies on a control-flow cycle, where per-iteration execution under divergent loop control would break
   barrier uniformity). A divergent write followed by a divergent read in the SAME region with no
   intervening uniform point cannot be ordered by any workgroup barrier, by uniformity: every point
   after the write and before the read is skipped by lanes not taking the branch, so a barrier there is
   dynamically non-uniform (deadlock/UB) instead of ordering. Same-lane write-then-read needs no barrier
   (program order) and is unaffected. The M3 fix is a structurizer transform, not barrier placement:
   split the region with an intermediate reconvergence (barrier at the intermediate merge, which all lanes
   execute) and re-diverge on the same condition for the read. Loop-carried LDS ordering across iterations
   of a divergently-controlled loop is likewise deferred to M3 (it needs loop-latch uniformity analysis).
