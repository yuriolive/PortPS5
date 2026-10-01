# PortPS5 — Spec: Guest memory

Status: draft v1 · 2026-09-27 · synced with `main` 2026-09-30

## Scope

Everything that places, protects, describes and tracks guest-visible memory: the libc arena and heap, the libkernel direct/flexible memory calls (`sceKernel*DirectMemory`, `*FlexibleMemory`, `mmap`/`mprotect`/`munmap`, `sceKernelVirtualQuery`, batch map), the allocation registry (leases, pins, generations, pin waiter), CPU write tracking, and the page-state cache the AGC driver reads. GPU-side caches that consume these facts are in [gpu-driver.md](gpu-driver.md). Paths are relative to `core/libs/prx/`; "main@e06dbff" is the pre-merge AnyPS5 main (old baseline). "main@75a8668" is the current AnyPS5 main and includes merged PR #5; its line numbers were re-checked there.

## Current state

| Area | AnyPS5 main@e06dbff (pre-merge) | AnyPS5 main@75a8668 (incl. merged PR #5) |
|---|---|---|
| Placement | Anywhere the host chooses. Every mapping is a pagefile section mapped twice, a guest view plus a host alias (`libc/src/MemoryBackingWindows.cpp:122-155`, alias at 133, guest view at 143). | One reservation from `0x10_0000_0000`, 64–448 GiB, below `MapAreaEnd = 0xFC_0000_0000` (`libc/src/GuestArena.cpp:18-21`, constructor 86-107). `MEM_WRITE_WATCH` when possible, and a plain reservation when not. The `APS5_NO_WRITE_WATCH` switch is at line 95. |
| Arena allocator | Heap chunks of 64 MiB, first-fit over a `std::map` free list (`libc/src/GuestHeapStorage.cpp:38-84`). | `Arena::Allocate` walks every used range: first-fit, O(n) per call (`GuestArena.cpp:43-55`). |
| Heap | Every block is carved from a chunk under the tracking `recursive_mutex`. | `ArenaHeap`: 64 size classes from 32 B to 64 KiB, bump-allocated from 1 MiB spans with a mutex per class (`libc/src/GuestHeap.cpp:92-101`). Blocks over 64 KiB are 64 KiB-granular and kept in a committed `LargeCache` capped by `APS5_HEAP_CACHE_MIB`, default 4 GiB (123-178). Each block carries a 16-byte header (204-207). |
| Registry | `GuestAllocations`: a `std::map` of `shared_ptr<const Range>`. A pin is inferred from `use_count() != 1`, and the mutation throws when pinned (`libc/src/GuestAllocations.cpp:142-151`). Protect and unmap copy the whole map (181-205). | Same structure under a plain `std::mutex`, plus a global `generation`, an `invalidator` and a `pinWaiter` (`GuestAllocations.cpp:34-36`). `RequireUnpinned` drops the lock, calls the waiter and retries, with a 60 s deadline and at most 10^6 idle rounds (184-234). `Add` and `Remove` record a change (170, 284), so **every guest malloc/free bumps the generation and runs the invalidator**. |
| Write tracking | A vectored exception handler plus `VirtualProtect` re-arm per page (`libc/src/MemoryTrackingWindows.cpp:16-76`), with a registry under a `recursive_mutex` (`GuestMemoryTracking.cpp:26-30,67-78`). | Fault tracking deleted. The driver's `WriteTracker` calls `GetWriteWatch(WRITE_WATCH_FLAG_RESET)` in one pass and stamps a `uint32` generation per 64 KiB block (`libSceAgcDriver/Execution/src/GuestMemory.cpp:618-664,718-802`), with a per-thread epoch memo. One tracker mutex covers all of it. Uncommitted pages make the walk fail, and the caller falls back to comparing bytes (767-770). |
| Page-state cache | — | `PageSpan`: 1 byte per 4 KiB page over the arena and main image, filled lazily from `VirtualQuery`, cleared by the invalidator, with a generation re-check against races (`GuestMemory.cpp:334-387,437-461,473-523`). |
| Direct memory | `MapAligned` goes to the section backing (`libkernel/DirectMemory/DirectMemory.cpp:87`). `sceKernelVirtualQuery` returns the 16 KiB page around the address (`Export.cpp:93-103`). | `DoMapDirect` validates `physStart`, then maps **fresh anonymous memory**, so two mappings of one physical range do not share bytes (`DirectMemory.cpp:318-337`). The physical pool is `bool _used[884736]` (13824 MiB in 16 KiB pages) with a linear scan (`MemoryPool.cpp:7,15-31,69-75,85`), plus an `_ranges` map that records each allocated block. Flexible memory has a 448 MiB budget (`Export.cpp:26,83-92`). `sceKernelVirtualQuery` scans a full lease linearly (`Export.cpp:208-267`). |
| Errors | EINVAL-class errors throw `std::invalid_argument`. The intended return codes survive only as comments (`DirectMemory.cpp:166-194,268,289` on main@75a8668; the same pattern appears in main@e06dbff). | Same. |

**Status as of 2026-09-30 (PortPS5 `main`).** The two tables above describe the AnyPS5 baselines. What PortPS5 has landed, all as building blocks the runtime does not instantiate yet:

| Piece | State on `main` | Where |
|---|---|---|
| Extent allocator | Landed (PR #32): treap with subtree-max, no throws, 10^6-operation differential test against the reference linear first-fit. Referenced only by tests; no arena uses it. | `libc/include/GuestArenaExtent.hpp`, `libc/src/GuestArenaExtent.cpp`, `tests/memory/GuestArenaExtentTests.cpp` |
| Direct memory and pools | Landed (PR #39): physical block tracking, pool exports, SCE error codes without host exceptions. | `libkernel/DirectMemory/MemoryPool.cpp`, `tests/memory/DirectMemoryPoolTests.cpp` |
| Write tracker | Landed (PR #40): `WriteWatchTracker` (64 KiB blocks, 64-bit global generations, 1 GiB shards), `PageStateTable`, explicit `PinToken`s, flush hook, `MarkWritten`. Referenced only by tests; the runtime still has `NullTracker` semantics. | `libc/include/WriteTracker.hpp`, `libc/src/WriteTracker.cpp`, `core/libs/tests/WriteTracker.cpp` |
| Tracking tests | Landed (PR #52): `GuestMemoryTracking::Watch` behaviour. | `tests/memory/MemoryTrackerTests.cpp` |
| Arena reservation, heap spans, registry interval map | Not landed. `GuestAllocations` is still the baseline `std::map` registry. | bean `portps5-421p` |

## Decision

Per the decision table in [README.md](README.md#subsystem-specs): **adopt** `GuestArena`/`GuestHeap`, and put write tracking behind `IWriteTracker`. On top of that:

- **Replace** the O(n) arena scan.
- **Replace** use-count pins with explicit pin counts.
- **Replace** the per-malloc registry entries.
- **Move** page-state ownership from the driver into this subsystem.
- **Drop** main@e06dbff's fault tracker and double-mapped sections as the default path.
- Return SCE error codes instead of throwing.

## Target design

**Layout.** Keep the AnyPS5 main (merged PR #5) reservation and the ascending first-fit order. The arena header comment says titles index tables by absolute address, so allocation order is part of the contract.

- Guest page size is 16 KiB (`PS5_PAGE_SIZE`). Host commit granularity is 4 KiB inside the arena.
- A fixed mapping outside the arena returns `SCE_KERNEL_ERROR_ENOMEM` and is logged. AnyPS5 main's 64 KiB-granular `VirtualAlloc` fallback (`DirectMemory.cpp:90-91`) is removed.

**Arena allocator.** Free extents live in an address-ordered balanced tree. Each node is augmented with the largest free extent in its subtree, the same technique as Linux `rb_subtree_gap`. The search descends to the lowest-address extent that fits, then checks alignment. That gives exactly the first-fit result in O(log n) for the aligned sizes the arena sees. *Correction from PR #32:* the `bytes + align - 16 KiB` pruning bound originally written here can skip a fitting extent (a 32 KiB extent at a 64 KiB-aligned base serving a 16 KiB request at 64 KiB alignment), so the implementation prunes only on `maxInSubtree >= bytes` and runs the exact aligned-fit check at each candidate. Its worst case is O(n) for a request that no extent can satisfy because of alignment padding. Free coalesces with both neighbours. A buddy allocator is rejected because it changes address order and rounds sizes up.

```cpp
struct Extent { u64 base, size; u64 maxInSubtree; Extent *l, *r, *parent; bool red; };
u64 FirstFit(u64 bytes, u64 align);   // expected O(log n), O(n) worst case when alignment padding blocks every fit; == reference linear scan
void Free(u64 base, u64 bytes);        // coalesce left/right, fix maxInSubtree up the path
```

**Heap.** Keep the AnyPS5 main (merged PR #5) size classes and spans.

- The registry records **spans** (1 MiB) and large blocks, not individual mallocs.
- Freeing a small block never waits on pins. Its memory stays committed, so a GPU read of a freed block sees stale bytes, never a fault.
- Only span release and large-block decommit mutate the registry.
- The `LargeCache` budget is sized automatically from physical memory. `debug.memory.heap_cache_mib` ([configuration.md](configuration.md)) overrides it for diagnostics only; release runs must not set it, and the results JSON records it when set.

**Registry.** An interval map guarded by an `SRWLOCK`: shared for queries and pins, exclusive for mutations.

```cpp
struct Range { u64 base, bytes, allocBase, allocBytes; u8 prot; u8 kind; // image|heap|direct|flexible
               std::atomic<u32> pins; u64 mapGen; };
PinToken Pin(std::span<const AddrRange>);        // shared lock, pins++ per overlapped Range
void Unpin(PinToken) noexcept;                    // pins--, WakeByAddressAll(&pins) on 0
u64 Generation();                                 // bumped only by map/unmap/protect/decommit
```

- `RequireUnpinned` keeps the AnyPS5 main (merged PR #5) contract: the waiter is called with the lock released, then the scan restarts. It waits with `WaitOnAddress` on `pins` instead of `yield`.
- A 60 s deadline breach goes to `Unsupported()` with a dump of the pinning submissions, instead of a throw ([threading.md](threading.md) §error policy).
- `sceKernelVirtualQuery` becomes a `lower_bound` plus an optional next-range step. It keeps AnyPS5 main's (merged PR #5) answer shape: the registered allocation extent and the protection bits.

**Direct memory.** The physical pool becomes the same extent tree over `[0, 13.5 GiB)`.

- Physical aliasing, where one `physStart` is mapped at two virtual addresses, cannot use write-watch memory: `MEM_WRITE_WATCH` applies only to private `VirtualAlloc` memory (inference from the Win32 contract; verify in M1).
- Default: private arena memory, write-watched, with no aliasing. The registry keeps `phys → mappings`.
- Aliasing support is M5 (ROADMAP). On a second mapping of a live physical range, both views move to a placeholder-backed section window (`VirtualAlloc2`/`MapViewOfFile3`) in the top of the guest range. Those ranges get `ProtectTracker`.
- Each alias event is logged and counted.

**Write tracking.**

`IWriteTracker` is the driver-facing interface of this subsystem, and it is defined only here. Its methods are `Collect`, `Pin`, `Unpin`, `PageState` and `MarkWritten`:

```cpp
using FlushHook = void (*)(void* ctx, u64 a, u64 n);    // lands pending GPU writes to [a, a+n)
struct IWriteTracker {
  virtual u64       Collect(u64 a, u64 n) = 0;          // stamp CPU-dirty blocks, return the newest block
                                                        // generation in the range; 0 = unknown -> compare bytes
  virtual PinToken  Pin(std::span<const AddrRange>) = 0;  // forwards to the registry
  virtual void      Unpin(PinToken) noexcept = 0;
  virtual PageState PageState(u64 addr) const = 0;      // lock-free page-state table read
  virtual u64       MarkWritten(u64 a, u64 n) = 0;      // GPU writes: bump block generations, return the new one
  virtual void      SetFlushHook(FlushHook, void* ctx) = 0;
};
```

- A range is unchanged since generation `g` when `Collect(range)` is non-zero and at most `g`. A zero result (uncovered range, failed walk) means the caller compares bytes.
- **Flush hook.** The driver registers one callback. Before a CPU read of a tracked range (a guest CPU access through a host path, a byte compare, a write-back source read), the tracker calls it for that range, and the driver lands the pending GPU writes before the read proceeds. The hook runs with no tracker lock held.
- Forgetting blocks on decommit and unmap is internal: the registry calls it under its exclusive lock.
- **Ownership.** Block generations, for both CPU writes (`Collect`) and GPU writes (`MarkWritten`, M3), live in the tracker. Which recorded batch wrote which range (writer intervals) lives in the driver's BufferCache ([gpu-driver.md](gpu-driver.md)).

| Implementation | Mechanism | Used for |
|---|---|---|
| `WriteWatchTracker` | AnyPS5 main's (merged PR #5) single resetting `GetWriteWatch` pass | Arena (default) |
| `ProtectTracker` | main@e06dbff's VEH + `VirtualProtect` | Aliased section views |
| `NullTracker` | Always reports "unknown" | Non-Windows, tracker unavailable |

- Block granularity stays at 64 KiB, and generations widen to 64 bits (see Failure modes).
- Tracker state is sharded by 1 GiB of address, each shard with its own lock, instead of one tracker mutex.
- Sub-block 4 KiB dirty bitmaps are enabled per block only where profiles show false invalidation (the decision table in [README.md](README.md#subsystem-specs)).
- Block-generation tracking for GPU-written surfaces uses `MarkWritten` (M3). In M1-M2 the driver keeps an interim general mechanism, "adjacent block-generation advance", with no switch and no title reference; M3 replaces it ([video-fmv.md](video-fmv.md)).

**Page-state table.** The registry, which knows every protection, writes 1 byte per 4 KiB page under its exclusive lock. The driver reads the table lock-free with relaxed atomics. `VirtualQuery` and the generation re-check race disappear from the hot path. Lookups are for arena and image pages only; any other address is "not guest memory".

## Interfaces

| Consumer | Contract |
|---|---|
| [gpu-driver.md](gpu-driver.md) | `IWriteTracker`: `Pin`/`Unpin` per submission, released at fence retire; `Collect`; `PageState(addr)`; `MarkWritten`; the flush hook. The pin waiter finishes leased GPU work and must not take guest-memory locks. Host-import ranges must be committed and unaliased. The driver owns writer intervals; this subsystem owns block generations. |
| [threading.md](threading.md) | Registry and tracker locks are host locks. They are never held across a guest callback or a futex wait. Pin waits use the shared futex primitive. |
| PRX libraries (image codecs first, [image-codecs.md](image-codecs.md)) | `GuestMemoryValidation::CheckReadable`/`CheckWritable` (`libc/include/GuestMemoryValidation.hpp`): noexcept, overflow-safe range checks returning `Ok`, `InvalidRange` (null, no access bits, `address + bytes` wraps), `Unmapped` or `AccessDenied`. The caller maps the status to its own SCE code. See "Validation API" below. |
| [relinker.md](relinker.md) | The main image is registered once, as non-releasable (`RegisterMainImage`). Its PE extent defines the image page span. |
| [video-fmv.md](video-fmv.md) | GPU-written planes are visible via `MarkWritten` generations. |
| [configuration.md](configuration.md) | `debug.memory.heap_cache_mib` (diagnostic override of the auto-sized heap cache). `[debug]` memory tracing (replaces `APS5_TRACE_QUERY`). No `APS5_*` switches. |
| [verification.md](verification.md) | Unit and microbenchmark targets below run in the `unit` job. `write_faults` goes into the results JSON; other counters go to the run log. |

### Validation API

`GuestMemoryValidateRange_nid_postfix(pointer, bytes, access)` (`libc/src/GuestMemoryValidation.cpp`) answers whether the host may read and/or write a guest range. It never throws and is safe from any thread.

- **Arithmetic.** `bytes` is compared against `UINT64_MAX - address` instead of computing `address + bytes` first, so a wrapping range is `InvalidRange` before any lookup. `bytes == 0` on a non-null pointer is `Ok`; a null pointer is always `InvalidRange`.
- **Registered ranges are authoritative.** `GuestAllocationsCover` walks the registry with a moving cursor, so a request may span several entries (an `mprotect` splits a mapping). A registered entry without the needed permission is `AccessDenied`; the host protection is not consulted, because the write tracker makes tracked pages read-only on the host while the guest still sees them as writable.
- **Unregistered gaps use the host mapping.** Thread stacks, TLS and data of modules loaded after the main image are not in the registry, and a title legitimately passes stack buffers. Those runs are checked against the host (Win32 `VirtualQuery`: `MEM_COMMIT`, no `PAGE_GUARD`/`PAGE_NOACCESS`, protection class; Linux `/proc/self/maps`, used by the unit tests). A hole is `Unmapped`.
- **Snapshot only.** A concurrent `munmap`/`mprotect` can invalidate the answer right after it returns. The check rejects bad pointers; it does not pin the range. Callers that must keep a range stable during a long operation need a pin (see the registry pins above).
- **Consumers.** `libSceJpegEnc` and `libScePngDec` check every param struct, handle header, work memory, pixel buffer, output buffer and `info` struct, and return their `INVALID_ADDR`/`INVALID_PARAM`/`INVALID_HANDLE` codes. Output buffers are validated for the bytes actually written, not for the title's claimed capacity.

## Failure modes

| Failure | Handling |
|---|---|
| Arena reservation fails (address space taken below 1 TiB) | Log and abort at start-up. The AnyPS5 main (merged PR #5) `malloc` fallback (`GuestHeap.cpp:184-190`) is removed because it breaks the address contract. |
| Arena or flexible budget exhausted | `SCE_KERNEL_ERROR_ENOMEM` to the guest, plus a log line with usage. |
| Invalid length, alignment or protection | `SCE_KERNEL_ERROR_EINVAL` return, never a throw. |
| Guest pointer from a PRX export is null, wraps, unmapped or lacks the access | `GuestMemoryValidation` status mapped to the library's own error code; the host never dereferences it. |
| Pin never released | Waiter rounds, then a 60 s deadline, then `Unsupported()` with the holders named. |
| 32-bit block generation wraps. AnyPS5 main@75a8668 `WriteTracker::generation` is a `std::atomic<uint32_t>` (`GuestMemory.cpp:636`), bumped on every collect (757). The rate is an inference: at about 10^5 collects/s it wraps in about 12 h. | 64-bit generations. |
| `GetWriteWatch` fails on uncommitted pages | Return "unknown", the caller compares bytes, and a counter is incremented. |
| Physical alias created after the GPU imported the range | Unpin, re-import through the section window, bump `mapGen`. |
| Global generation churn from malloc | Removed: heap blocks no longer touch the registry. |

## Tests

- **GoogleTest Unit Suites** (`ctest -L unit`, hosted `unit` job):
  - Extent tree differential-tested against a reference linear first-fit, with identical addresses required. Hosted `unit` run: 3 seeds x 10^6 operations at a live-set cap of 1024 (a few seconds; the reference scan is O(live)). The full uncapped 10^6-operation run (`GuestArenaExtent.FuzzUncapped`, label `slow`, live set grows to ~22k, ~3 min) runs in the scheduled `nightly` workflow via `ctest --preset slow`.
  - Heap class boundaries, alignment invariants, and flexible memory pools.
  - Registry pin, wait and release, with the waiter observed to run with the lock released.
  - Generation bumps only on map, unmap, protect and decommit.
  - `sceKernelVirtualQuery` exact and find-next cases.
  - Write-watch collect and unchanged cases, and `MarkWritten` versus CPU stamps.
  - Page-state table differential against `VirtualQuery`.
  - Alias detection moving both views to the section window.
  - Death tests (`EXPECT_DEATH`): verify that memory allocation attempts violating the 1 TiB boundary contract fail cleanly without memory corruption.
- **Ported Ecosystem Test Suites:**
  - **KytyPS5 `VirtualMemoryAllocationTests`:** 16 KB page rounding, direct-memory allocations (`sceKernelAllocateDirectMemory`), alignment constraints, protection transitions (`PROT_READ`, `PROT_WRITE`, `PROT_EXEC`), and out-of-memory error codes (`SCE_KERNEL_ERROR_ENOMEM`).
  - **KytyPS5 `MemoryTrackerTests`:** ported in behaviour onto `GuestMemoryTracking::Watch` (`tests/memory/MemoryTrackerTests.cpp`): range validation, page rounding, protection and fault resolution, invalidate on unmap, the resolver-must-release and no-re-entry contracts (death tests), and concurrent publication and resolution.
    - [x] Dirty-page collecting and generation advancement now have a counterpart in `WriteWatchTracker` (PR #40) and are covered by `core/libs/tests/WriteTracker.cpp` (`Collect`, `MarkWritten`, global generations, pins, flush hook).
    - [ ] Aliased-memory tracking is not ported: it needs alias support (M5).
  - **FreeBSD 12 `mmap`/`mprotect` Suites:** POSIX address-space layout and page-permission semantics.
  - **Wine / Proton Virtual Memory Suites:** Win32 `VirtualAlloc`/`VirtualProtect`/`GetWriteWatch` state transitions under concurrent queries.
- **Microbenchmarks:**
  - `malloc`/`free` throughput per size class, with no registry lock on the small path.
  - Arena allocation with 10^5 live ranges, O(log n).
  - `VirtualQuery` at 10^5 ranges.
  - `Collect` cost per MiB, clean and dirty.
- **Local regression** (verification.md §2): the run log counts pin waits, compare fallbacks, alias events and ENOMEM returns per run. Protect-tracker faults go into the results JSON as `write_faults`.

## Milestones

| Milestone | Delivers |
|---|---|
| M1 | - [ ] Port `GuestArena`/`GuestHeap` behind `IWriteTracker` (ROADMAP M1; bean `portps5-421p`):<br>- [x] extent tree (PR #32, unwired);<br>- [x] explicit pins, the flush hook and the page-state table in `WriteWatchTracker` (PR #40, unwired);<br>- [x] return codes replace throws in direct memory and pools (PR #39);<br>- [ ] arena reservation and `GuestHeap` use them, with span-level heap registration and the registry interval map;<br>- [ ] the runtime instantiates the tracker and the driver registers the flush hook;<br>- [x] no `APS5_*` memory switches remain in `core/`. |
| M3 | - [ ] General block-generation write tracking for GPU-written surfaces (`MarkWritten`), replacing the interim adjacent block-generation advance, and the tracker-side support for the capture-ordering redesign (ROADMAP M3). |
| M5 | - [ ] Direct-memory aliasing through section windows, and full-size streaming and resource aliasing. Host-import budget sized automatically (ROADMAP M5). |

## Open questions

1. Do any gate titles map one physical direct range twice? The M1 import inventory plus the alias counter will answer this. If none do, the section window stays dormant.
2. Can a `MEM_WRITE_WATCH` reservation coexist with placeholder splitting? This is unverified, and it decides where the alias window lives.
3. Does any title rely on `sceKernelDirectMemoryQuery` returning real extents? AnyPS5 main@75a8668 returns the recorded block from its pool `_ranges` map and a single page only for unrecorded offsets (`Export.cpp:136-153`, `MemoryPool.cpp:46-54`).
4. Is a 64 KiB block granularity too coarse for per-job label slots? AnyPS5 main@75a8668 notes slots 0x20 apart (`GuestMemory.cpp:628-630`). Decide from profiles.
5. Sync assessment (PR #28): upstream `GuestArena` (`libc/src/GuestArena.cpp`, `include/GuestArena.hpp`) was trial-ported and reverted. Verdict: a raw port cannot land — the O(n) first-fit scan (`GuestArena.cpp:43-55` on AnyPS5 main@75a8668) contradicts the extent-tree decision below, `throw std::runtime_error` sites break the no-throw rule (a guest `catch(...)` swallows host exceptions), the file is unwired, and there are no file headers. It returns with the M1 extent-tree port (adapted, return codes, headers); the extent-tree core landed in PR #32 and the wiring is open (bean `portps5-421p`). `HostThreadLocal.hpp` stays: it compiles and its GTest balance suite is green.
6. Guest-memory range-validation API for PRX libraries: resolved by `GuestMemoryValidation` (see "Validation API"; bean `portps5-8l0d`). It uses the allocation registry plus a host-mapping fallback rather than the page-state table, because the table covers only tracked arena pages. Remaining: more PRX consumers (audio, net, file libraries) still read guest buffers raw; each needs its own pass and tests.
