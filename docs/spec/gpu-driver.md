# PortPS5 — Spec: GPU driver (AGC / PM4 / Vulkan)

Status: draft v1 · 2026-09-27 · synced with `main` 2026-09-30

## Scope

This spec covers `core/libs/prx/libSceAgcDriver` and the PM4 builders in `core/libs/prx/libSceAgc`. It describes how the guest's PM4 command buffers become Vulkan work, and covers:

- PM4 parsing per queue, labels and waits;
- the Recorder;
- guest buffer and texture residency (host import, staging, detile);
- resource capture for the recompiler;
- rasterizer state, draws, dispatches and presentation.

Shader translation is in [shader-recompiler.md](shader-recompiler.md). Persistence of SPIR-V and pipelines is in [pipeline-cache.md](pipeline-cache.md). The guest arena and write tracking are in [guest-memory.md](guest-memory.md). Unless noted, file references are relative to `core/libs/prx/libSceAgcDriver/`, in one of two AnyPS5 trees: `main@e06dbff` (pre-merge main, the old baseline) or `main@75a8668` (current main, which includes merged PR #5).

## Current state

**Common to both trees.** `libSceAgc` builds PM4 packets into guest command buffers. The builders are `APS5_VABI` exports, such as `sceAgcDcbDrawIndex` (`libSceAgc/DcbDraw/src/DrawIndexed.cpp:12`), `sceAgcDcbDrawIndirect` (`DrawNonIndexed.cpp:12`) and `sceAgcAcbDispatchIndirect` (`Acb/src/Dispatch.cpp:12`). The driver decodes those buffers when they are submitted. Opcode names are in `Execution/include/Pm4Opcodes.hpp`.

**`main@e06dbff`:**

- One worker thread serves every queue (`Execution/src/Driver.cpp:300`).
- `VulkanDevice::Dispatch` creates a module, layout, pipeline and fence per call, then waits on them (`Execution/src/VulkanDevice.cpp:759-800`).
- Indirect draws (`0x24`, `0x25`, `0x2c`, `0x38`) are rejected (`Execution/src/Pm4.cpp:104`).
- Depth/stencil **state and surface** are implemented (M2, this repo): `DecodeDepthStencil`/`DecodeDepthTarget` (`Graphics/src/State.cpp`) decode the registers, `ResidentDepth` (`Graphics/src/DepthSurface.cpp`) owns a host-only `VkImage` per surface, and the render pass carries it as an attachment (see "Depth/stencil" under Target design). Not implemented: retile to guest memory, guest-memory upload of never-cleared surfaces, MSAA/array/mip depth, partial clears and two-pass conditional colour writes; each is an explicit logged rejection.
- The graphics pipeline cache keys on the full SPIR-V bytes and keeps 128 entries (`Graphics/src/GraphicsPipelineCache.cpp:90-98,129`).

**Landed in PortPS5 (PR #49, bean `portps5-5gcb`, M1 Lane A):** `Graphics/include/Recorder.hpp` + `Graphics/src/Recorder.cpp` and `Graphics/include/HostImport.hpp` + `Graphics/src/HostImport.cpp`, adapted from AnyPS5 `8a69fefe` (merged PR #5; AnyPS5 `main`'s `Recorder` has moved on with read tracking and later fixes, and this port is based on `8a69fefe`):

| Piece | Behaviour | Differs from upstream |
|---|---|---|
| `Recorder` | One open batch per device, serials on a timeline semaphore (`WaitSerial`, `FinishUpTo`, `CompletedSerial`), pending-write snapshot read lock-free by the flush hook, label table (`NoteLabel`, `PendingLabel`, `LookupLabel`), `AfterCompletions` with the "GPU already stored it" rule, in-order completions. | Owns a recursive lock (`Recorder::Scope`) instead of `GuestMemory::GpuMutex`. No `getenv` switches, profiling tables or release thread (kept objects die after the outermost `Scope` ends). Flush hook registered through `IWriteTracker::SetFlushHook`. |
| `HostImport` | `Bind(address, bytes, Read/Write)` returns a `VkBuffer` + device address: an import of the aligned guest window (`VK_EXT_external_memory_host`, `HOST_COHERENT` type, every page tracked `ReadWrite`), else a staging copy refreshed when `IWriteTracker::Collect` moves and written back on completion. Budgeted; LRU eviction never while an unfinished batch used the import. Bad ranges return `BindStatus`, not exceptions. | Budget is `HostImportOptions::importBudgetBytes` (0 forces staging: the spec's test hook); the automatic sizing stays M5. No `APS5_HOST_IMPORT_MIB`. |
| Device | `VulkanDevice` enables `VK_KHR_timeline_semaphore` and `VK_EXT_external_memory_host` when present and reports them through `Graphics::Context::externalMemoryHost` / `hostImportAlignment`. | - |

Not yet wired (verified 2026-09-30: nothing outside `tests/` constructs `Recorder` or `HostImport`, and `sceAgcDriverSubmitDcb` at `Submit/src/Dcb.cpp:31` does not use them): `VulkanDevice::Dispatch`/`Draw` still execute synchronously and do not record into the `Recorder`; moving them (and `sceAgcDriverSubmitDcb`) onto `Recorder` + `HostImport` is the remaining M1 driver-port work (bean `portps5-tiod`). The `lavapipe`-labelled suites exist but `ci.yml` has no `driver-lavapipe` job (bean `portps5-ekx3`). Tests: `tests/RecorderTests.cpp`, `tests/HostImportTests.cpp` (`agc_recorder_tests`, label `lavapipe`).

**`main@75a8668`, the merged PR #5 work** (`Driver.cpp` is 6,305 lines, `Graphics/src/Recorder.cpp` is 2,910 lines):

| Area | What `main@75a8668` does | Where |
|---|---|---|
| Queues | `Driver::QueueWorker` runs one thread per guest queue, so a `WAIT_REG_MEM` blocks only its own queue. Device work is serialised by `GuestMemory::GpuMutex`, and every queue shares one `VkQueue`. | `Driver.cpp:852-860` (`QueueWorker`), `:1524-1527` (thread start), `:6132 run()`, `:761` (shared `VkQueue`) |
| Recorder | Keeps one open batch. Submissions are numbered by serials on a timeline semaphore (`WaitSerial`, `FinishUpTo`). It tracks pending writes and reads (`NotePendingWrite`, `PendingReadOverlaps`) and runs completion actions (write-backs). | `Graphics/include/Recorder.hpp:25-40,95-165` |
| Labels | Labels are deferred per worker (`DeferredLabels`). A pending-label table (`NoteLabel`, `NoteQueuedLabel`, `LookupLabel`) lets a `WAIT_REG_MEM` take a label's value **before the GPU executes it**, under a "late trust" rule. | `Driver.cpp:375-383`; `Recorder.hpp:210-283` |
| Capture | `ShaderMemory::Capture` reads SRT chains and descriptors from guest memory **on the CPU**, in the unlocked prologue of a draw or dispatch. Pages with pending GPU writes are read word by word through the flush hook. `recordQueuedLabelsAfterCapture` restarts a packet whose capture overlapped a label its own queue has queued. | `Execution/include/ShaderMemory.hpp:11-60`; `Driver.cpp:465-472,3516,4388,5066` |
| Host import | `VK_EXT_external_memory_host` imports guest allocations. They are capped by `APS5_HOST_IMPORT_MIB` (default 6 GiB). A refused import falls back to CPU copies. | `Graphics/src/GuestBufferMemory.cpp:158-182` |
| BDA | `BdaResources` holds a page table buffer (guest range to device address) and a fault buffer that the shader-side BDA path reads. | `Graphics/include/BdaResources.hpp` |
| Detile | `TextureDetiler::Dispatch` runs `TextureDetile.comp` (GFX10 XOR swizzle equations as specialisation constants). The same shader also retiles. | `Graphics/include/TextureDetiler.hpp:45`; `Graphics/shaders/TextureDetile.comp:1-20` |
| Textures | `StorageTexture` write-back with 64 KiB write stamps. The "adjacent generation" special case exists for video planes packed back to back. | `Graphics/src/Texture.cpp:839-850` |
| Indirect draws | `resolveIndirectDraw` parses all four opcodes. The driver takes a GPU path (`vkCmdDraw[Indexed]Indirect`, or `vkCmdDraw[Indexed]IndirectCountKHR` for a GPU-side count) when the CP's SGPR patch folds (`Rule::InPlace`, or `Rule::Constant` with the records copied and patched) and the records come from a host import. **Otherwise it reads the records on the CPU**; `IndirectDrawPath` names the reason (`NotFolded`, `DrawIndex`, `IndxOffset`, `VertexRange`, `FeatureGap`, `NotImported`, `PendingImage`, `PendingLabelOrCopy`, ...). Vertex ranges are capped by `APS5_INDIRECT_VERTEX_MIB`. A device without `VK_KHR_draw_indirect_count` resolves GPU-count draws on the CPU. | `Execution/src/Pm4.cpp:545-586`; `Driver.cpp:4495-4521,4651`; `Graphics/include/Draw.hpp:42-48`; `Graphics/src/Draw.cpp:825-832`; `Execution/src/VulkanDevice.cpp:711-714` |
| Depth/stencil | Rejected: `DB_DEPTH_CONTROL` (cx `0x200`), depth bounds and conditional colour writes. The only handling is the `APS5_IGNORE_DEPTH_TEST` debug switch (draws as if tests always passed) and a pass-through check for ALWAYS/no-write tests (AnyPS5 `main` `Graphics/src/State.cpp` `depthPassThrough`). No depth image. | `Graphics/src/State.cpp:320-322,520` |
| Bindless | Material scans are capped at 256 records (`MaterialScanLimit`) and 16 slots (`BindlessSlots`, clamped to 1–48). | `core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp:185,855` |
| Title HLE | `matchesFillKernel` matches an exact 9-dword kernel. `matchesCopyKernel` matches a code hash and V# words. | `Driver.cpp:1692,1929` |
| Failure | `tolerate()`/`reportSkip()` skip a draw or dispatch that throws. The recompiler's `planFailure` memo rethrows forever. | `Driver.cpp:1644-1660`; `core/shader/recompiler/Recompiler.cpp:190,264-268` |

The driver module alone has 360 lines calling `getenv("APS5_…")` (summed `git grep -c`).

**The capture-ordering race.** Hardware reads descriptors and SRT words when it executes a draw. `main@75a8668` reads them when it records the draw, on another thread, while earlier work may still be unexecuted. A capture's CPU read goes stale in four cases:

- (a) An earlier batch writes the range but has not completed. The flush hook makes the read wait, which is correct but stalls.
- (b) The capturing queue has queued a label that is not yet recorded. `recordQueuedLabelsAfterCapture` handles this by restarting the packet.
- (c) A `WAIT_REG_MEM` was satisfied early from the label table, and the work that produces the data read after the wait has not executed, or (on another queue) has not even been decoded.
- (d) GPU writes into memory that was not imported reach guest memory only through a completion write-back.

In cases (c) and (d) a pointer word is read before it has been written. The SRT walk then dereferences an unwritten pointer plus an offset, which is the observed "guest memory is not readable at 0x60" (`Execution/src/GuestMemory.cpp:560`). *Inference:* 0x60 is a zero base plus a field offset. The dispatch is then skipped (`tolerate`), and the plan-failure memo keeps it skipped. Widening batches widens the window.

### Upstream Recorder delta since `8a69fefe`

PortPS5 PR #49 ports the Recorder from AnyPS5 commit `8a69fefe` (`perf(agc): batch GPU work in a recorder with host-imported guest memory`). AnyPS5 `main@75a8668` has since changed it in four commits. Sizes below are `wc -l` on each tree; line numbers are `main@75a8668`.

| Commit | Change to the Recorder |
|---|---|
| `29b4601` (merged PR #5) | `Graphics/src/Recorder.cpp` grows from 1,592 to 2,901 lines (2,910 at `main@75a8668`); `Graphics/include/Recorder.hpp` grows from 262 to 688 lines. All items in the table below. `tests/Recorder.cpp` is new (945 lines; 983 at `main@75a8668`). |
| `e424b6b` | Per-thread recorder state (`DeferredBatches`, `QueuedLabelRanges`, `LabelGroupDwords`) moves from `thread_local` to `HostThreadLocal<T, Tag>` (`Graphics/src/Recorder.cpp:282`, `:482`), so it survives Windows thread teardown. |
| `76f1d48` | Debug-only: `Draw.cpp` samples small in-place input ranges at record time and compares them at execution (`APS5_CAPTURE_INPUTS`, requires `APS5_CAPTURE_TRACE`). Not portable as written: a new environment switch. |
| `9f1c680` | `ShaderResources::PrepareDrawBindings` returns a `DrawBindings` that holds per-draw input buffer snapshots until GPU completion (`ShaderResources.hpp:117`, used at `Draw.cpp:1111`). Fixes a record-vs-execute race on in-place inputs. Adds a test to `tests/Recorder.cpp`. |

What `29b4601` adds to the Recorder itself (`Recorder.hpp`):

| Area | Additions | Where |
|---|---|---|
| Read tracking | `ReadKind` (dispatch element, GPU copy, address-based, indirect, storage upload, copy source), `NotePendingRead[s]`, `PendingReadOverlaps`, `DescribePendingRead`, `ReadCounts`. Lets a CPU write wait only for batches that still read the range. | `:111-140` |
| Late labels | `LabelHit` with a `late` flag and stamp, `LabelRefusal` reasons, `PendingLabel` / `LookupLabel` / `LookupLabelValue` with `afterStamp`, `NoteQueuedLabel`, `ForgetQueuedLabels`, `LateTrust`, `CloseLabelGroup`. A `WAIT_REG_MEM` may take a label value before the GPU writes it. | `:235-258` |
| Write settling | `PendingWriteSnapshot`, `SnapshotOverlaps`, `PendingWriteSettled`, `PublishGeneration`, `InCompletion`, `ThreadHookWaits`. | `:294-369` |
| Store runs | `RecordStore` / `FlushStores[Overlapping]` coalesce CPU-to-GPU stores into runs; `QueueKeyStore` / `FlushKeyStores` do the same for DCC key stores; `StoreCounts` reports joins, refused joins and WAW barriers. | `:74`, `:202`, `:377` |
| Render pass reuse | `ContinuesRenderPass`, `LeaveRenderPassOpen`, `CommandsInRenderPass` keep a pass open across compatible draws. | `:67` |
| Hazard tracker | `CommandClass` (17 classes), `Access`, `NoteAccess`, `MergeBarriers`, `BarrierValidate`, `CountBarriers`: barrier elision by simulated access tracking, with a validation mode. | `:404-430` |
| GPU timing | Per-class timing through `BeginGpuTiming(CommandClass)`, `EndGpuTiming`, `AddGpuTiming`, `GpuTimingEnabled`. | `:404-410` |
| Completion | `CompletedBatches`, `NewestSubmitted`, `CountPresent`, `Presents`, `FlipReadCheck`. | `:460-465` |

What PR #49 should take, in order:

1. [ ] **Re-base on `main@75a8668`'s `Recorder.hpp` and `.cpp`** instead of `8a69fefe`, since `8a69fefe` alone has no read tracking, late labels, store runs or hazard tracker. Cite `29b4601` in the commit body.
2. [ ] **`e424b6b`:** take `HostThreadLocal` for the three per-thread vectors. It needs `prx/libc/include/HostThreadLocal.hpp` in the fork (check it exists first).
3. [ ] **`9f1c680`:** take `DrawBindings` snapshots. This is a real correctness fix, so add its regression test.
4. [ ] **Skip `76f1d48`** (env-switch debug aid); if wanted, map it to a `[debug]` key.
5. [ ] **Env switches:** `Recorder.cpp` has 14 `getenv("APS5_")` calls at `8a69fefe` and 32 at `main@75a8668`. The 19 distinct names added since (32 calls, 31 distinct names in total) are `APS5_BARRIER_VALIDATE`, `APS5_COPY_READ_TRACKING`, `APS5_COUNT_ALL_COMPLETION_LABELS`, `APS5_DCC_KEYS_EACH`, `APS5_FLIP_READ_CHECK`, `APS5_FULL_BARRIERS`, `APS5_HOOK_FLUSH_CPU_BLOCKS`, `APS5_HOOK_FULL_SYNC`, `APS5_LABEL_RUNS_INLINE`, `APS5_LABEL_TRUST_LATE`, `APS5_NO_BARRIER_ELISION`, `APS5_NO_HOOK_COMPLETION_GUARD`, `APS5_NO_JOIN_WAW_CHECK`, `APS5_NO_LABEL_BATCHING`, `APS5_NO_LABEL_RUNS`, `APS5_NO_PROC_TABLE`, `APS5_NO_SEPARATE_QUEUED_LABELS`, `APS5_TRACE_BARRIERS`, `APS5_TRACE_CAPSYNC`. Each becomes a typed `[debug]` key or is deleted with its fallback path. Switches that select an alternative algorithm (`APS5_NO_*`) are removed, keeping the default path.
6. [ ] **Tests:** `tests/Recorder.cpp` (983 lines) uses bare `Require()` and a hand-written runner (`readTrackingTests`, `writeSettledTests`, `completionCountTests`, `labelTests`, `lateLabelTests`, `unchangedSinceTests`, `keyProofTests`, `closeRaceTests`, `storeRunTests`, `resourceReadTests`, `dataWordPositionsTests`). The fork requires GoogleTest through `portps5_add_gtest`; port each function to a `TEST_F` on a lavapipe fixture. It asserts `Recorder::ReadTracking()` is on, which goes away with `APS5_COPY_READ_TRACKING`.

## Decision

This follows the decision table in [README.md](README.md#subsystem-specs) §GPU driver:

- Adopt AnyPS5 main's (merged PR #5) Recorder, host import and GPU detile.
- Replace CPU-side capture: buffers are resolved on the GPU through device addresses, images at submit time behind the ordering fence, under the label-wait rule below.
- Split the driver into seven modules.
- Add depth/stencil, the indirect family and conditional colour writes.
- Size the host-import budget automatically and fall back to staging.
- Use a GPU-side descriptor heap for bindless.
- Recognise fill and copy kernels by general IR patterns.
- Raise the API floor to Vulkan 1.3. It is the documented reference tier (PRD §4.4). Emitted SPIR-V stays at 1.3 (1.4 for mesh shaders) until subgroup size is pinned per pipeline, because SPIR-V 1.6 lets the driver vary the subgroup size per dispatch (see Open questions). Vulkan 1.4 is not required: it shares the 1.6 SPIR-V ceiling, and its additions (push descriptors, `maintenance5/6`, host image copy, dynamic-rendering local read) are optional optimizations to adopt behind a capability check once a before/after measurement justifies them.
- Do not adopt: `matchesFillKernel`, `matchesCopyKernel`, the bindless caps, `planFailure`, `tolerate`-skips, or any `APS5_*` behaviour switch.

## Target design

**Modules** (under `libSceAgcDriver/`, replacing `Driver.cpp`):

| Module | Owns | From |
|---|---|---|
| `CommandProcessor` | Decodes PM4 for each guest queue (one thread each), keeps the register shadow (`QueueState`), handles labels and waits, runs `INDIRECT_BUFFER`, and orders submits. It emits typed commands to the Recorder. | `Pm4.cpp`, the queue half of `Driver.cpp` |
| `Recorder` | The open batch, serials, the timeline, pending read/write intervals, completions, and the submit-time resolve hook. | `Graphics/Recorder.cpp` |
| `BufferCache` | Host imports, the staging fallback, the BDA page table, and writer intervals (which recorded batch wrote which range). Block generations live in the tracker ([guest-memory.md](guest-memory.md)). | `GuestBufferMemory.cpp`, `BdaResources.cpp` |
| `TextureCache` | Images keyed by (guest address, T# shape), detile/retile, write-back, and descriptor-heap slots. | `Texture.cpp`, `TextureCache.cpp`, `TextureDetiler.cpp` |
| `PipelineCache` | Variant to `VkPipeline`, plus the disk format (see [pipeline-cache.md](pipeline-cache.md)). | `Pipeline.cpp`, `PipelineCache.hpp` |
| `Rasterizer` | Translates context registers into Vulkan state: blend, depth/stencil, raster, viewport and dynamic state. | `State.cpp`, `Draw.cpp` |
| `Presenter` | Flips, the swapchain, the scaler, and the `VideoOut` completion events. | `Presentation*`, `VideoOutput.hpp` |

**Resource resolution.** Each shader resource is split into a *shape* and *values*:

- The **shape** is `ResourceSpecialization`: format, stride, dimension, numeric class. The recompiler needs it to pick a variant.
- The **values** are base addresses, sizes and descriptor words.

Resolution then follows three rules:

1. **Buffers are resolved on the GPU.** The shader loads the V# and the SRT chain from guest memory at execute time through the BDA page table, using the existing `bdaAbiVersion` ABI (see [shader-recompiler.md](shader-recompiler.md)). No CPU read happens at record time, so cases (a) to (d) cannot arise for buffers. Out-of-range pointers resolve to the fault buffer, and faults are reported, never fatal on the GPU.
2. **Images are resolved at submit time.** A draw records with deferred image slots (descriptor sets with `UPDATE_AFTER_BIND`). When the Recorder submits, a `resolve` step first waits on the timeline for `max(dependency serials)`, then reads the T# words, looks up or creates the images through the TextureCache, and writes the descriptors.
3. **Shape words must be settled.** When the shape itself lies in a range with pending GPU writes, the CommandProcessor splits the batch: it submits, waits on the producer's serial, and then captures. Each split is counted as a `capture_split` stall in telemetry.

**Label-wait rule.** Suppose a `WAIT_REG_MEM` W on queue Q is satisfied early by a label L that batch B recorded and that has not yet executed. Q then records `minResolveSerial[Q] = max(minResolveSerial[Q], serial(B))`, and every later resolve and settled-shape check on Q waits for that serial first. When a capture on Q cannot be deferred (for example `COPY_DATA` from memory), W waits for B instead. Indirect records are never read on the CPU, so they never force this case. A wait is therefore never satisfied from an unexecuted label while a capture on that queue depends on it. The GPU-side order needs no extra work, because all queues share one `VkQueue` in submission order. The "late trust" rule stays as it is, and CPU stores are still checked with `IWriteTracker::Collect` ([guest-memory.md](guest-memory.md)).

```text
on WAIT_REG_MEM(Q, addr, ref):
  hit = labels.Lookup(addr)            // recorded or same-queue queued entry
  if hit && hit.batch.executed: satisfy
  elif hit && Q.nextCapturesDeferrable: satisfy; Q.minResolveSerial = max(.., hit.batch.serial)
  else: recorder.WaitSerial(hit ? hit.batch.serial : poll)
```

**Block-generation tracking** (M3) replaces AnyPS5 main's (merged PR #5) adjacent-generation special case. In M1-M2 that case survives only as an interim general mechanism, "adjacent block-generation advance", with no switch and no title reference.

- Block generations, CPU and GPU, live in the tracker. The driver reads them with `Collect` and reports GPU writes with `MarkWritten` ([guest-memory.md](guest-memory.md)).
- The BufferCache keeps the writer intervals: an interval map `[begin,end) → (writerId, gpuGen)` of which recorded batch wrote which range.
- A write-back calls `MarkWritten` only for the bytes it wrote.
- A surface is unchanged when `Collect` over it reports no newer generation, ignoring GPU intervals the surface itself owns.

Surfaces packed into a shared block (video planes, for example) therefore no longer invalidate each other. [video-fmv.md](video-fmv.md) depends on this.

**Host-import budget and staging.** The budget is set once at startup (as a proposal, to be tuned in M5) to `clamp(0.5 × (total physical memory − 8 GiB), 2 GiB, 24 GiB)`. Imports are evicted LRU by last-used serial, and never while an in-flight batch still references them. A range that is refused or misaligned gets a VMA staging buffer, filled with the dirty blocks since its last generation and written back when its batch completes. A refused import changes performance only, never results. The `APS5_HOST_IMPORT_MIB` override becomes `debug.gpu.host_import_mib` ([configuration.md](configuration.md)), a diagnostic override of the auto-sized budget: release runs must not set it, and the results JSON records it when set.

**Depth/stencil.** The Rasterizer translates `DB_DEPTH_CONTROL` (cx `0x200`), the stencil control, reference and mask registers, and depth bounds (when `VkPhysicalDeviceFeatures::depthBounds` is available) into the pipeline and dynamic state. Depth surfaces are host-owned `VkImage`s in the TextureCache. They are retiled to guest memory only when the CPU or a shader reads them as textures. *Inference:* HTILE metadata can start out as always-decompressed.

**Implemented in M2 (state decode).** `DecodeDepthStencil` (`Graphics/src/State.cpp`) decodes `DB_DEPTH_CONTROL` (cx `0x200`), `DB_STENCIL_CONTROL` (`0x10b`), `DB_STENCILREFMASK`/`_BF` (`0x10c`/`0x10d`) and the depth-bounds pair (`0x8`/`0x9`) into `State::depthStencil`; unset stencil registers read as their reset value 0. `ZFUNC`/`STENCILFUNC` (0-7) map numerically to `VkCompareOp`; stencil ops map to `VkStencilOp` (enum per AMD gfx10 register database: 0 KEEP, 1 ZERO, 2 ONES, 3 REPLACE_TEST, 4 REPLACE_OP, 5 ADD_CLAMP, 6 SUB_CLAMP, 7 INVERT, 8 ADD_WRAP, 9 SUB_WRAP, 10 AND, 11 OR, 12 XOR, 13 NAND, 14 NOR, 15 XNOR). Vulkan has one reference per face that is both the compare reference and the `REPLACE` value, while the hardware has three replace sources (REPLACE_TEST writes the test value, REPLACE_OP the op value, ONES writes 0xff). All replace-type ops that can write (write mask nonzero) must agree on one value under the write mask; when that value differs from the test value the reference is repurposed as the replace value, which is sound only when the stencil function ignores the reference (NEVER/ALWAYS); otherwise the face is rejected. ADD/SUB need `STENCILOPVAL == 1`; XOR is `INVERT` when every written bit is set in the op value and `KEEP` when none is; AND, OR, NAND, NOR, XNOR and partial XOR are rejected (Vulkan has no equivalent, and unlike the replace case no reference trick applies). A zero write mask makes every op a `KEEP`. `Z_WRITE_ENABLE` without `Z_ENABLE` is inert and cleared (upstream `52ffef82`). The back face mirrors the front unless `BACKFACE_ENABLE` is set. Reserved `DB_DEPTH_CONTROL` bits (11-19, 23-29) are rejected. `DB_DEPTH_VIEW` `Z_READ_ONLY`/`STENCIL_READ_ONLY` (bits 24/25) drop depth writes and turn every stencil op into a `KEEP`.

**Depth surface (implemented in M2).** `DecodeDepthTarget` decodes the bound surface into `State::depthTarget`:

| Register (cx) | Field | Use |
|---|---|---|
| `DB_Z_INFO` (`0x10`) | `FORMAT` bits 0-1: 0 none, 1 Z16, 3 Z32_FLOAT (2 is reserved and rejected) | Depth aspect format. With `DB_STENCIL_INFO.FORMAT` (`0x11`, bit 0) also decides whether a surface is bound at all; with neither set no surface register is read. |
| `DB_Z_INFO` | `NUM_SAMPLES` (2-3), `PARTIALLY_RESIDENT` (12), `MAXMIP` (16-19) | Nonzero rejected (host image is one single-sample 2D layer). `SW_MODE`, `TILE_SURFACE_ENABLE`, expclear and other layout/HTILE bits are guest-memory-layout metadata and inert because the image is host-owned. |
| `DB_Z_WRITE_BASE`/`_HI` (`0x14`/`0x1c`), `DB_STENCIL_WRITE_BASE`/`_HI` (`0x15`/`0x1d`) | address = `(HI << 40) \| (LO << 8)`, HI is 8 bits | Identify the surface across draws; never dereferenced. Null bases and read bases (`0x12`/`0x13`, `_HI` `0x1a`/`0x1b`) that differ from the write bases are rejected. |
| `DB_DEPTH_SIZE_XY` (`0x7`) | `X_MAX` bits 0-13, `Y_MAX` bits 16-29 (value is size minus one) | Surface extent; the register must have been written. |
| `DB_DEPTH_VIEW` (`0x2`) | slice start/max, mip (26-29), `Z_READ_ONLY` (24), `STENCIL_READ_ONLY` (25) | Slices and mips must be zero; read-only bits as above. |
| `DB_RENDER_CONTROL` (`0x0`) | `DEPTH_CLEAR_ENABLE` (0), `STENCIL_CLEAR_ENABLE` (1) | Clears. `DEPTH_COPY`/`STENCIL_COPY` (2, 3) and reserved bits are rejected; resummarize, compress-disable, copy-sample and decompress bits are inert metadata. |
| `DB_DEPTH_CLEAR` (`0xb`), `DB_STENCIL_CLEAR` (`0xa`) | float in [0, 1]; low byte | Clear values. |

*Host image.* `ResidentDepth` (`Graphics/src/DepthSurface.cpp`) creates one `VkImage` per surface, keyed in `RenderCache::GetDepth` by its guest write base (a changed base, extent or format replaces it with a fresh, undefined one). The format comes from the guest format and the device's depth-attachment support (`Graphics/src/DepthFormat.cpp`): Z16 maps to D16 (else D32F), Z32F to D32F, Z16+stencil to D24S8 (else D32FS8, since no 16-bit depth+stencil format exists), Z32F+stencil only to D32FS8 (D24 would quantise a float depth), stencil-only to S8 (else a combined format). A surface no candidate can host is rejected with a logged error. The attachment sits after the optional colour attachment in the render pass and framebuffer; `RenderCache`, `GraphicsPipelineCache` (view, loadOps and clear flags in the key) and `Draw` hold it alive until the draw completes. Consecutive draws are separate render passes, so `ResidentDepth::Begin` records the first `UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL` transition and a fragment-test barrier between draws. `ToVulkan(state, target, depthBoundsSupported)` reconciles the guest state with the bound aspects: no depth aspect drops depth test, write and bounds; no stencil aspect drops the stencil test; an aspect cleared by this draw neither tests nor writes.

*Clears.* A draw with `DEPTH_CLEAR_ENABLE`/`STENCIL_CLEAR_ENABLE` on a bound aspect becomes render-pass `loadOp = CLEAR` for that aspect with the `DB_DEPTH_CLEAR`/`DB_STENCIL_CLEAR` value, and the draw itself neither tests nor writes the cleared aspect (hardware writes the clear value instead of running the tests). Because `CLEAR` clears the whole attachment, the draw must be a rect-list (the AMD clear convention) whose scissor and render extent cover the entire depth surface; a triangle-list clear or a smaller scissor is rejected (`ValidateDepthClear`). Partial clears (`vkCmdClearAttachments` over the scissor) are not built.

*Definedness.* The surface lives only on the host; guest memory is never read or written. Each aspect is "defined" only after a clear. A draw that reads an aspect (depth test, depth bounds, stencil test) that was never cleared is rejected with a logged error: its contents would be whatever the guest left in memory through a path the host cannot see (compute or DMA clears), and guessing a value would render silently wrong. Write-only draws on an undefined aspect are allowed and leave it undefined (`loadOp = DONT_CARE`); later loads use `LOAD`.

*Cache bound.* `RenderCache::GetDepth` keeps at most 32 surfaces. At capacity it evicts the least recently used surface that holds no defined data, and only when every other surface holds cleared contents the least recently used defined one (`SelectDepthEviction`); it never evicts the surface being requested and never the whole cache. The image is the only copy of the depth data, so a defined surface that has to go loses its contents: this is logged, and a later read of it is rejected as never-cleared. Surviving that needs guest-memory backing, which is the retile work listed as out of scope. Draws and cached pipelines that still reference an evicted surface keep its image alive.

*Depth bounds.* `depthBounds` is enabled at device creation when available (`VulkanDevice.cpp`, `Context::depthBounds`). A draw that uses the bounds test on a bound depth aspect on a device without the feature, or with bounds outside [0, 1] without `VK_EXT_depth_range_unrestricted`, is rejected with a logged error; with no depth aspect (or while clearing) the test is dropped.

*Trigger (Open question 8).* The host image is allocated on the first draw that binds a surface (nonzero `DB_Z_INFO.FORMAT` or `DB_STENCIL_INFO.FORMAT`), independent of whether a test is enabled in that draw, because a clear or a later test needs the image to exist. A surface is never allocated for a draw with neither format set, which keeps 2D layering without a depth buffer inert.

*Stencil op summary (revisited).* Relaxed: REPLACE_OP and ONES now map to `REPLACE` through the face reference when the stencil function ignores it. Kept as rejections because Vulkan cannot express them: ADD/SUB with `STENCILOPVAL != 1` (Vulkan steps by exactly one), AND/OR/NAND/NOR/XNOR, partial XOR, and replace ops that write different values in one face (or a reference-reading function with a differing replace value).

Tests: `tests/DepthStencilState.cpp` (GoogleTest, `agc_driver_depth_stencil_tests`: register decode, rejection paths, read-only view, `ToVulkan` reconciliation, depth-bounds gating, conditional colour writes) and `tests/DepthSurface.cpp` (`agc_driver_depth_surface_tests`: format policy, loadOp mapping, `ResidentDepth` lifetime and barriers against a mock Vulkan device); the colour-mask checks stay in `tests/Graphics.cpp`.

**Not built (Out of scope for M2).** Retile to guest memory, including the CPU or a shader reading the depth surface as a texture, needs a depth-format-aware detile/tile path (`Graphics/src/TextureTiling.cpp` is colour-only) and is deferred to an M2 follow-up; until then a shader that samples a guest depth buffer reads guest memory the host never wrote. Also deferred: guest-memory upload of never-cleared surfaces, MSAA, array and mip depth, separate read and write bases, partial clears, HTILE-aware paths (M5, Open question 2).

**Conditional colour writes** (`DB_DEPTH_CONTROL` bit 30 `ENABLE_COLOR_WRITES_ON_DEPTH_FAIL`, bit 31 `DISABLE_COLOR_WRITES_ON_DEPTH_PASS`) are decoded into `DepthStencilState` and applied by `ApplyConditionalColorWrites` once the colour target is known. Colour is normally written where the depth test passes. Decision per combination, for a draw with a colour target: (0, 0) unchanged. Bit 31 alone suppresses colour everywhere, which Vulkan expresses as a zero `colorWriteMask` while depth and stencil still run, so it is implemented, with no dynamic state needed. Bits 30+31 ("colour only where depth fails") are also "never" when the depth test cannot fail (no depth test or no depth aspect), so the mask is zeroed; with a depth test that can fail they need a second pass with an inverted compare (and the colour pass must precede the depth-writing pass), which also re-runs the pixel shader and interacts with stencil, so it is rejected with a logged error. Bit 30 alone means colour is written on depth pass and depth fail; it is inert while the depth test cannot fail and rejected when it can. `VK_EXT_color_write_enable` was considered and dropped: it toggles colour writes per draw but cannot depend on the per-pixel depth result, so it adds nothing over the zero mask. *Inference:* a disabled depth test counts as "pass" for bit 31 (the AMD register documentation describes the bits in terms of the depth test result). Stencil operations are decoded only when `STENCIL_ENABLE` is set: a disabled test passes every pixel, so stale ops left in the registers never reject a draw.

**Indirect family.** `DRAW_INDIRECT`, `DRAW_INDEX_INDIRECT`, `DRAW_INDIRECT_MULTI` and `DRAW_INDEX_INDIRECT_MULTI` always take the GPU path, through `vkCmdDraw[Indexed]Indirect[Count]`. When the CP's SGPR patch does not fold (AnyPS5 main's `NotFolded`, `DrawIndex` and `IndxOffset` cases), a patch compute pass copies the record fields into a per-draw user-data buffer that the shader reads for those SGPRs (a recompiler contract). Vertex buffers go through BDA, so the vertex-range cap goes away. `DISPATCH_INDIRECT` maps to `vkCmdDispatchIndirect`. No indirect record is ever read on the CPU.

**Bindless.** The baseline is one descriptor heap built with `VK_EXT_descriptor_indexing` (core in Vulkan 1.2): partially bound, update-after-bind arrays of sampled images, storage images and samplers, sized from the device limits. `VK_EXT_descriptor_buffer` is an optional backend. The TextureCache gives every resident view a heap slot. At submit time it builds, for the table ranges a shader declares, a GPU hash table from T# address to slot, which the shader probes. This replaces `MaterialScanLimit` and `BindlessSlots`. When the heap is full, slots are evicted LRU by serial.

**Fill and copy patterns.** After SSA and folding, the recompiler returns a `KernelIdiom` in `RecompileResult.idiom` ([shader-recompiler.md](shader-recompiler.md)) in two general cases:

- *uniform store:* every invocation stores invocation-independent values at an address affine in the global id, with no loads and no other side effects;
- *linear copy:* `dst[a·i+b] = src[a·i+c]`, with bounds taken from `num_records`.

The driver executes a `KernelIdiom` as `vkCmdFillBuffer` or `vkCmdCopyBuffer` only when the ranges are resolved at submit time. Otherwise, as with an indirect count, it runs the shader.

### Per-draw CPU cost

Converted titles miss the 60 Hz flip rate in draw-heavy scenes (about 53 draws and 530 PM4 packets per frame against about 16 in light frames), so the cost is per draw. Metrics from one gate title on the `release` preset (the `dev` preset runs SPIR-V tools per rect-list draw and is about 2.5x slower; never measure on it): `Driver.Draw.total` about 30 ms per slow frame, `Graphics.ShaderResources.bindings` about 12 ms, fence waits 6 to 7 ms, `Driver.Draw.shaders` about 3.8 ms, `memory_upload` and `vertex_upload` about 3 ms each, `GuestMemory.Read` about 3.6 ms over about 880 calls, and `TextureCache::Get` about 0.15 ms per lookup even on a hit. Read them with `[debug] profile = ["gpu"]`.

Slices, each its own PR with a regression test that needs no game data. A texture changed by the guest or by GPU write-back must still be re-read in every slice.

| # | Mechanism | Status |
|---|---|---|
| 1 | `BytesEqual` (SSE2, `memcmp == 0` semantics) for the TextureCache whole-texture revalidation and the `GuestBufferMemory` snapshot consistency checks. Cuts the compare cost, keeps exact semantics. | - [x] done in PR #75 |
| 2 | Replace the per-draw compare with write-watch invalidation. The existing `GuestMemoryTracking::Watch` is page-protection based (`PAGE_NOACCESS` / `PAGE_READONLY`) and requires committed, writable, non-executable memory (`MemoryTrackingWindows.cpp` `Query`). A read-only texture page makes a kernel write into it (a file read, DMA) fail with an error instead of faulting, so this needs a design decision before code. | - [ ] open, see Open question 10 |
| 3 | Reuse prepared `ShaderResources` state (layout, descriptor writes, vertex layout) when shaders, bindings and descriptors are unchanged between draws (the idea behind AnyPS5 `29b4601` draw recipes, behaviour only). | - [ ] open |
| 4 | Batch guest reads and uploads (`GuestMemory.Read` calls, vertex and memory upload). | - [ ] open |

Out of scope here: the render-target-as-texture copy (`RenderTexture.cpp`), owned by another change.

## Interfaces

| Peer | Contract |
|---|---|
| [shader-recompiler.md](shader-recompiler.md) | `RecompileRequest` and `ResourceSpecialization` (shape only), the BDA ABI version, SGPRs sourced from a user-data buffer, `KernelIdiom` (in `RecompileResult.idiom`), the descriptor-heap probe, and the subgroup size. |
| [pipeline-cache.md](pipeline-cache.md) | The driver hands over stable pipeline descriptions keyed by variant content hash, never by the process-local `variantId`. It counts `pipeline_creations` and `spirv_compilations`. |
| [guest-memory.md](guest-memory.md) | `IWriteTracker` (`Collect`, `Pin`, `Unpin`, `PageState`, `MarkWritten`) and the flush hook, both defined there; GuestArena alignment to `minImportedHostPointerAlignment`. The driver registers the flush hook and owns writer intervals; the tracker owns block generations. |
| [threading.md](threading.md) | Queue threads block on the futex primitive. `GpuMutex` is split into Recorder-, BufferCache- and TextureCache-local locks. The watchdog observes `packetsDone`. |
| [video-fmv.md](video-fmv.md) | Block-generation tracking for GPU-written planes, and the ordering of `VideoOut` flip events. |
| [input.md](input.md) | The Presenter owns the SDL window, and input reads its events. |
| [configuration.md](configuration.md) | `display.resolution_scale` and `display.present_mode` (vsync), `[debug]` (`debug.gpu.trace`, `debug.gpu.dump_frames`, `debug.gpu.host_import_mib`; diagnostic only, never set in release runs, recorded in the results JSON when set), and `[workarounds]` keys that name mechanisms only. |
| [build-toolchain.md](build-toolchain.md) | Internal shaders (`TextureDetile.comp`, fill/patch passes) are compiled by glslang at build time and embedded (`embed_spirv.cmake`). |
| [verification.md](verification.md) | `driver-lavapipe` tests, telemetry fields, and the `policy` job (no hash-matched kernels). |

## Failure modes

| Condition | Behaviour |
|---|---|
| No Vulkan 1.3 device with graphics, compute and (when presenting) swapchain support. | `VulkanDevice` construction throws `no Vulkan 1.3 ...`. Devices reporting a lower `apiVersion` are skipped during selection. |
| Register state has no translation. | `Unsupported()` logs the packet and the register dump, then aborts. Nothing is silently skipped. |
| Recompile or `KernelIdiom` execution plan fails. | Logged once, then abort via `Unsupported()`. No skip, and no `planFailure` memo. |
| A shape word depends on pending GPU work. | Batch split plus a timeline wait, counted in the `capture_split` telemetry. |
| Import refused, misaligned or over budget. | Staging buffer. Results are identical and slower, and the refusal is logged once per range class. |
| A BDA load hits an unmapped range. | Reads the fault buffer (zeros) and records the fault. The fault is reported after the batch. A repeated fault at one site is fatal in verify runs. |
| Descriptor heap exhausted. | LRU eviction. If every slot is still in flight, the batch is split and eviction retried. |
| Waits make no progress for more than 30 s. | The watchdog flags a softlock and dumps the per-queue PM4 positions and the label table ([verification.md](verification.md) §3). |
| `VK_ERROR_DEVICE_LOST`. | Logs the last recorded serials and packet history (`PacketHistory`), then aborts. |

## Tests

| Layer | Tests |
|---|---|
| unit (hosted `ctest -L unit`) | GoogleTest suites: PM4 parse and size tables (extending `tests/Pm4.cpp`); state translation per register field; label-wait rule state machine; interval map and block-generation logic; `KernelIdiom` recognition on synthetic IR (also in `recompiler-golden`). Death tests (`EXPECT_DEATH`): verify that unsupported packets or invalid draw descriptors abort via `Unsupported()`. |
| ecosystem reference ports (hosted & lavapipe) | **DXVK & RPCS3 Test Patterns:** buffer/texture cache overlap invalidation, staging ring-buffer allocation and exhaustion, descriptor pool fragmentation, and multi-queue synchronization barrier hazard tests. |
| `driver-lavapipe` (hosted) | Recorder ordering and completion order (extending `tests/Recorder.cpp`, which already creates a real instance); detile then retile is the identity for every tile mode; buffer-cache invalidation after a CPU write; staging path with the budget forced to 0 through a test hook (not an env var); two surfaces in one 64 KiB block; a cross-queue test where queue A writes an SRT pointer then a label and queue B waits and dispatches; depth/stencil and indirect-family synthetic PM4; fill and copy transfers byte-identical to running the shader; descriptor-heap probe. |
| local regression | Frame checks per [verification.md](verification.md) §2; the Demon's Souls stress run (3 × 150 s intro) with 0 wedges and 0 skips in the "not readable" class; `capture_split` in the results JSON, with BDA fault counts in the run log. |

## Milestones

| Milestone | Delivers |
|---|---|
| M1 | - [ ] Port the Recorder (- [x] landed in PR #49, unwired), host import with staging fallback (- [x] landed in PR #49, unwired), and GPU detile (- [x]); wiring them into the driver's submit path is open (bean `portps5-tiod`). Keep the interim adjacent block-generation advance (no switch, no title reference) until M3. Remove `matchesFillKernel`, `matchesCopyKernel`, `tolerate`-skips, the failure memo and the `APS5_*` switches (fill and copy run as the title's own shaders). Add the `driver-lavapipe` CI job. |
| M2 | - [x] Depth/stencil and conditional colour-write state decode, Vulkan depth-stencil create info, pipeline-cache keying. - [x] Host depth surface (`DB_Z_*` decode, host `VkImage`, render-pass attachment, `DB_RENDER_CONTROL` clears, depth bounds, bit 31 colour suppression; PR #55, bean `portps5-mij8` completed). - [ ] Retile depth to guest memory (texture reads of a depth buffer) and guest-memory depth upload (bean `portps5-9s7e`). |
| M3 | - [ ] Module split. The indirect family. Block-generation tracking. The capture-ordering redesign (GPU buffers, submit-time images, the label-wait rule). |
| M4 | - [ ] GPU-side descriptor heap for bindless. Fill/copy IR pattern recognition. Wave64 through subgroup-size control, in step with the recompiler. |
| M5 | - [ ] Automatic host-import budget, full-size streaming and aliasing, and the performance pass. |
| M6 | - [ ] Release full runs, with nothing new in the driver. |

## Open questions

1. When the TextureCache misses at submit time, should it create the image synchronously (a stall) or use GPU miss feedback and create it on the next frame? Feedback is lossy, so a synchronous miss is the default.
2. HTILE and DCC: is always-decompressed good enough for performance, or is a metadata-aware path needed in M5?
3. Is one shared `VkQueue` sufficient, or do compute queues need an async-compute `VkQueue`, which would need cross-queue timeline waits?
4. Does the budget formula need a per-vendor factor? This will be measured in M5.
5. Is `VK_EXT_descriptor_buffer` worth a second backend, given driver maturity on all three vendors?
6. Correction for the decision table in [README.md](README.md#subsystem-specs): it says "`DRAW_INDIRECT` is not implemented". That is true of `main@e06dbff`, but `main@75a8668` (merged PR #5) implements the family with a GPU path for folded SGPR patches and a CPU record-read fallback for the rest (table row "Indirect draws"). This spec keeps the M3 item and scopes it as "GPU path only, no CPU record reads".
7. Sync assessment (PR #28): upstream `Recorder` (`Graphics/src/Recorder.cpp`, 2,901 lines at the trial port; 2,910 at `main@75a8668`) was trial-ported and reverted. Verdict: a raw port cannot land — 32 `getenv("APS5_…")` calls trip the `policy` job by design, throws cross the `APS5_VABI` boundary, and the file is unwired without the driver port. It returns adapted (typed `[debug]` config, return codes/abort path, file headers, GTest under `driver-lavapipe`) together with the M1 driver port. The `tests/Recorder.cpp` assessment likewise waits for the ported driver headers. **Update (PR #49):** the adapted `Recorder` and `HostImport` landed without the GpuMutex coupling, release thread or env switches (see "Landed in PortPS5" above); wiring the driver's dispatch/draw/submit path onto them is still open (bean `portps5-tiod`).
8. Depth surface (resolved in M2): the host image is allocated on the first draw that binds a surface; clears map to `loadOp = CLEAR` (rect-list, full-surface only); bit 31 colour suppression is a zero colour write mask, bit 30 with a failable depth test is a rejection; see "Depth surface" above. Remaining: retile depth to guest memory and upload of never-cleared surfaces (titles that clear through compute or DMA are rejected on their first depth read), and whether a partial clear path (`vkCmdClearAttachments`) is worth building.
9. Emit SPIR-V 1.6? It needs `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` set to `target.subgroupSize` (plus `REQUIRE_FULL_SUBGROUPS` for compute) on every pipeline path, with the `subgroupSizeControl` and `computeFullSubgroups` features enabled, the size checked against the device's min/max range, and each pinned stage checked against `VkPhysicalDeviceSubgroupSizeControlProperties::requiredSubgroupSizeStages` (support is per stage and implementation-dependent; an unsupported stage is rejected with a logged error). `REQUIRE_FULL_SUBGROUPS` is set on a compute pipeline only when `local_size_x` is a multiple of `target.subgroupSize`, which Vulkan requires; any other workgroup size omits the flag or is rejected with a logged error. Until then a 1.6 module may run a wave64 guest shader on a different subgroup size (between draws and dispatches, and where the stage allows it within one command) and silently read the wrong lanes. This lands with the wave64 work in M4.
10. Per-draw texture revalidation: can write-watch invalidation replace the whole-texture compare? Page protection breaks kernel writes into watched pages (they fail instead of faulting) and costs a fault per guest write. Options to evaluate with measurements: keep the compare but gate it by a cheap per-page dirty probe (Windows write-watch via `MEM_WRITE_WATCH` and `GetWriteWatch`, which does not change protection), or restrict watching to textures the guest has not written for N frames. Not decided; the compare stays until a measured design exists.
