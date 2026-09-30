# PortPS5 — Spec: GPU driver (AGC / PM4 / Vulkan)

Status: draft v1 · 2026-09-27

## Scope

This spec covers `core/libs/prx/libSceAgcDriver` and the PM4 builders in `core/libs/prx/libSceAgc`. It describes how the guest's PM4 command buffers become Vulkan work, and covers:

- PM4 parsing per queue, labels and waits;
- the Recorder;
- guest buffer and texture residency (host import, staging, detile);
- resource capture for the recompiler;
- rasterizer state, draws, dispatches and presentation.

Shader translation is in [shader-recompiler.md](shader-recompiler.md). Persistence of SPIR-V and pipelines is in [pipeline-cache.md](pipeline-cache.md). The guest arena and write tracking are in [guest-memory.md](guest-memory.md). Unless noted, file references are relative to `core/libs/prx/libSceAgcDriver/`, in AnyPS5 `main` (`e06dbff`) or PR #5 (`29b4601`).

## Current state

**Common to both trees.** `libSceAgc` builds PM4 packets into guest command buffers. The builders are `APS5_VABI` exports, such as `sceAgcDcbDrawIndex` (`libSceAgc/DcbDraw/src/DrawIndexed.cpp:12`), `sceAgcDcbDrawIndirect` (`DrawNonIndexed.cpp:12`) and `sceAgcAcbDispatchIndirect` (`Acb/src/Dispatch.cpp:12`). The driver decodes those buffers when they are submitted. Opcode names are in `Execution/include/Pm4Opcodes.hpp`.

**`main`:**

- One worker thread serves every queue (`Execution/src/Driver.cpp:300`).
- `VulkanDevice::Dispatch` creates a module, layout, pipeline and fence per call, then waits on them (`Execution/src/VulkanDevice.cpp:759-800`).
- Indirect draws (`0x24`, `0x25`, `0x2c`, `0x38`) are rejected (`Execution/src/Pm4.cpp:104`).
- Depth/stencil is rejected (`Graphics/src/State.cpp:152`).
- The graphics pipeline cache keys on the full SPIR-V bytes and keeps 128 entries (`Graphics/src/GraphicsPipelineCache.cpp:90-98,129`).

**PR #5** (`Driver.cpp` is 6,214 lines, `Graphics/src/Recorder.cpp` is 2,901 lines):

| Area | What PR #5 does | Where |
|---|---|---|
| Queues | `Driver::QueueWorker` runs one thread per guest queue, so a `WAIT_REG_MEM` blocks only its own queue. Device work is serialised by `GuestMemory::GpuMutex`, and every queue shares one `VkQueue`. | `Driver.cpp:790-799`, `:6048 run()`, `:623-627` |
| Recorder | Keeps one open batch. Submissions are numbered by serials on a timeline semaphore (`WaitSerial`, `FinishUpTo`). It tracks pending writes and reads (`NotePendingWrite`, `PendingReadOverlaps`) and runs completion actions (write-backs). | `Graphics/include/Recorder.hpp:20-40,93-177` |
| Labels | Labels are deferred per worker (`DeferredLabels`). A pending-label table (`NoteLabel`, `NoteQueuedLabel`, `LookupLabel`) lets a `WAIT_REG_MEM` take a label's value **before the GPU executes it**, under a "late trust" rule. | `Driver.cpp:330-400`; `Recorder.hpp:210-283` |
| Capture | `ShaderMemory::Capture` reads SRT chains and descriptors from guest memory **on the CPU**, in the unlocked prologue of a draw or dispatch. Pages with pending GPU writes are read word by word through the flush hook. `recordQueuedLabelsAfterCapture` restarts a packet whose capture overlapped a label its own queue has queued. | `Execution/include/ShaderMemory.hpp:20-60`; `Driver.cpp:431-439,1613,4990` |
| Host import | `VK_EXT_external_memory_host` imports guest allocations. They are capped by `APS5_HOST_IMPORT_MIB` (default 6 GiB). A refused import falls back to CPU copies. | `Graphics/src/GuestBufferMemory.cpp:155-200` |
| BDA | `BdaResources` holds a page table buffer (guest range to device address) and a fault buffer that the shader-side BDA path reads. | `Graphics/include/BdaResources.hpp` |
| Detile | `TextureDetiler::Dispatch` runs `TextureDetile.comp` (GFX10 XOR swizzle equations as specialisation constants). The same shader also retiles. | `Graphics/include/TextureDetiler.hpp:45`; `Graphics/shaders/TextureDetile.comp:1-20` |
| Textures | `StorageTexture` write-back with 64 KiB write stamps. The "adjacent generation" special case exists for video planes packed back to back. | `Graphics/src/Texture.cpp:838-849` |
| Indirect draws | `resolveIndirectDraw` parses all four opcodes. The driver takes a GPU path when the CP's SGPR patch folds (`Rule::InPlace`, `Rule::Constant`), and **otherwise falls back to reading the records on the CPU**. Vertex ranges are capped by `APS5_INDIRECT_VERTEX_MIB`. | `Execution/src/Pm4.cpp:507-530`; `Driver.cpp:4410-4450` |
| Depth/stencil | Rejected: `DB_DEPTH_CONTROL` (cx `0x200`), depth bounds and conditional colour writes. | `Graphics/src/State.cpp:320-322,520` |
| Bindless | Material scans are capped at 256 records (`MaterialScanLimit`) and 16 slots (`BindlessSlots`, clamped to 1–48). | `core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp:184,857` |
| Title HLE | `matchesFillKernel` matches an exact 9-dword kernel. `matchesCopyKernel` matches a code hash and V# words. | `Driver.cpp:1623,1860` |
| Failure | `tolerate()`/`reportSkip()` skip a draw or dispatch that throws. The recompiler's `planFailure` memo rethrows forever. | `Driver.cpp:1575-1591`; `core/shader/recompiler/Recompiler.cpp:187-190,262-268` |

The driver module alone has 357 lines calling `getenv("APS5_…")` (summed `git grep -c`).

**The capture-ordering race.** Hardware reads descriptors and SRT words when it executes a draw. PR #5 reads them when it records the draw, on another thread, while earlier work may still be unexecuted. A capture's CPU read goes stale in four cases:

- (a) An earlier batch writes the range but has not completed. The flush hook makes the read wait, which is correct but stalls.
- (b) The capturing queue has queued a label that is not yet recorded. `recordQueuedLabelsAfterCapture` handles this by restarting the packet.
- (c) A `WAIT_REG_MEM` was satisfied early from the label table, and the work that produces the data read after the wait has not executed, or (on another queue) has not even been decoded.
- (d) GPU writes into memory that was not imported reach guest memory only through a completion write-back.

In cases (c) and (d) a pointer word is read before it has been written. The SRT walk then dereferences an unwritten pointer plus an offset, which is the observed "guest memory is not readable at 0x60" (`Execution/src/GuestMemory.cpp:560`). *Inference:* 0x60 is a zero base plus a field offset. The dispatch is then skipped (`tolerate`), and the plan-failure memo keeps it skipped. Widening batches widens the window.

## Decision

This follows the decision table in [README.md](README.md#subsystem-specs) §GPU driver:

- Adopt PR #5's Recorder, host import and GPU detile.
- Replace CPU-side capture: buffers are resolved on the GPU through device addresses, images at submit time behind the ordering fence, under the label-wait rule below.
- Split the driver into seven modules.
- Add depth/stencil, the indirect family and conditional colour writes.
- Size the host-import budget automatically and fall back to staging.
- Use a GPU-side descriptor heap for bindless.
- Recognise fill and copy kernels by general IR patterns.
- Raise the API floor to Vulkan 1.3 and emit SPIR-V 1.6 for every stage. 1.3 is the documented reference tier (PRD §4.4) and the first core version whose SPIR-V ceiling is 1.6, so a 1.1 instance or device would reject the modules. Vulkan 1.4 is not required: it keeps the same SPIR-V ceiling, and its additions (push descriptors, `maintenance5/6`, host image copy, dynamic-rendering local read) are optional optimizations to adopt behind a capability check once a before/after measurement justifies them.
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

**Block-generation tracking** (M3) replaces PR #5's adjacent-generation special case. In M1-M2 that case survives only as an interim general mechanism, "adjacent block-generation advance", with no switch and no title reference.

- Block generations, CPU and GPU, live in the tracker. The driver reads them with `Collect` and reports GPU writes with `MarkWritten` ([guest-memory.md](guest-memory.md)).
- The BufferCache keeps the writer intervals: an interval map `[begin,end) → (writerId, gpuGen)` of which recorded batch wrote which range.
- A write-back calls `MarkWritten` only for the bytes it wrote.
- A surface is unchanged when `Collect` over it reports no newer generation, ignoring GPU intervals the surface itself owns.

Surfaces packed into a shared block (video planes, for example) therefore no longer invalidate each other. [video-fmv.md](video-fmv.md) depends on this.

**Host-import budget and staging.** The budget is set once at startup (as a proposal, to be tuned in M5) to `clamp(0.5 × (total physical memory − 8 GiB), 2 GiB, 24 GiB)`. Imports are evicted LRU by last-used serial, and never while an in-flight batch still references them. A range that is refused or misaligned gets a VMA staging buffer, filled with the dirty blocks since its last generation and written back when its batch completes. A refused import changes performance only, never results. The `APS5_HOST_IMPORT_MIB` override becomes `debug.gpu.host_import_mib` ([configuration.md](configuration.md)), a diagnostic override of the auto-sized budget: release runs must not set it, and the results JSON records it when set.

**Depth/stencil.** The Rasterizer translates `DB_DEPTH_CONTROL` (cx `0x200`), the stencil control, reference and mask registers, and depth bounds (when `VkPhysicalDeviceFeatures::depthBounds` is available) into the pipeline and dynamic state. Depth surfaces are host-owned `VkImage`s in the TextureCache. They are retiled to guest memory only when the CPU or a shader reads them as textures. *Inference:* HTILE metadata can start out as always-decompressed.

**Conditional colour writes** (`DB_DEPTH_CONTROL` bits `0xc0000008`, today rejected at `State.cpp:322`) are mapped to colour-write-enable dynamic state where their semantics allow it. Otherwise they are a documented `Unsupported()`.

**Indirect family.** `DRAW_INDIRECT`, `DRAW_INDEX_INDIRECT`, `DRAW_INDIRECT_MULTI` and `DRAW_INDEX_INDIRECT_MULTI` always take the GPU path, through `vkCmdDraw[Indexed]Indirect[Count]`. When the CP's SGPR patch does not fold (PR #5's `NotFolded`, `DrawIndex` and `IndxOffset` cases), a patch compute pass copies the record fields into a per-draw user-data buffer that the shader reads for those SGPRs (a recompiler contract). Vertex buffers go through BDA, so the vertex-range cap goes away. `DISPATCH_INDIRECT` maps to `vkCmdDispatchIndirect`. No indirect record is ever read on the CPU.

**Bindless.** The baseline is one descriptor heap built with `VK_EXT_descriptor_indexing` (core in Vulkan 1.2): partially bound, update-after-bind arrays of sampled images, storage images and samplers, sized from the device limits. `VK_EXT_descriptor_buffer` is an optional backend. The TextureCache gives every resident view a heap slot. At submit time it builds, for the table ranges a shader declares, a GPU hash table from T# address to slot, which the shader probes. This replaces `MaterialScanLimit` and `BindlessSlots`. When the heap is full, slots are evicted LRU by serial.

**Fill and copy patterns.** After SSA and folding, the recompiler returns a `KernelIdiom` in `RecompileResult.idiom` ([shader-recompiler.md](shader-recompiler.md)) in two general cases:

- *uniform store:* every invocation stores invocation-independent values at an address affine in the global id, with no loads and no other side effects;
- *linear copy:* `dst[a·i+b] = src[a·i+c]`, with bounds taken from `num_records`.

The driver executes a `KernelIdiom` as `vkCmdFillBuffer` or `vkCmdCopyBuffer` only when the ranges are resolved at submit time. Otherwise, as with an indirect count, it runs the shader.

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
| M1 | - [ ] Port the Recorder, host import with staging fallback, and GPU detile. Keep the interim adjacent block-generation advance (no switch, no title reference) until M3. Remove `matchesFillKernel`, `matchesCopyKernel`, `tolerate`-skips, the failure memo and the `APS5_*` switches (fill and copy run as the title's own shaders). Add the `driver-lavapipe` CI job. |
| M2 | - [ ] Depth/stencil and conditional colour writes. Pipeline-cache integration. |
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
6. Correction for the decision table in [README.md](README.md#subsystem-specs): it says "`DRAW_INDIRECT` is not implemented". That is true of `main`, but PR #5 implements the family with a CPU fallback. This spec keeps the M3 item and scopes it as "GPU path only, no CPU record reads".
7. Sync assessment (PR #28): upstream `Recorder` (`Graphics/src/Recorder.cpp`, 2,901 lines) was trial-ported and reverted. Verdict: a raw port cannot land — 32 `getenv("APS5_…")` calls trip the `policy` job by design, throws cross the `APS5_VABI` boundary, and the file is unwired without the driver port. It returns adapted (typed `[debug]` config, return codes/abort path, file headers, GTest under `driver-lavapipe`) together with the M1 driver port. The `tests/Recorder.cpp` assessment likewise waits for the ported driver headers.
