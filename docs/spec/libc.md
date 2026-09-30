# PortPS5 — Spec: libc replacement library

Status: draft v1 · 2026-09-30

## Scope

The exports of the replacement `libc.prx` that guest code calls directly: the mspace allocator (`sceLibcMspace*`), the application heap front-ends (`malloc` family, `operator new/delete`, `aligned_alloc`), strings and bounds-checked (`_s`) functions, narrow and wide formatting and scanning, stdio extras, and the process and thread lifecycle exports (`cxa_atexit`, `cxa_finalize`, `init_env`, thread-exit destructors).

Not covered here: locale and iostream ABI data ([relinker.md](relinker.md) Open questions; PR 48), the guest arena and write tracking ([guest-memory.md](guest-memory.md)), pthread and sync ([threading.md](threading.md)), and the C++ exception runtime. Paths are relative to `core/libs/prx/libc/` unless stated. "AnyPS5 main" is `upstream/main` of the AnyPS5 fork source.

## Current state

| Area | PortPS5 (this tree) |
|---|---|
| Mspace | `src/Mspace.cpp`: ordered allocator. Metadata lives on the host heap (`std::map` of chunks plus a `std::set` of free chunks keyed by size), so guest writes cannot corrupt it. Best-fit, coalescing on free, in-place realloc growth, memalign, reallocalign, `MallocStats*`, nested mspaces, thread-unsafe flag accepted. Ported from AnyPS5 `3058980d`, `02b431eb`, `8f982168`. |
| Application heap | `src/ApplicationHeap.cpp` dispatches to the title's allocator replacement table. A table whose slots are all empty falls back to the guest heap (AnyPS5 `67fce999`). A partially filled table is still rejected. |
| Heap front-ends | `src/HeapExtras.cpp`: the `operator new/delete` family, `std::nothrow`, `set_new_handler`/`get_new_handler`, `aligned_alloc`, `reallocalign(nullptr, ...)`. `src/AllocatingStrings.cpp`: `strndup`, `asprintf`. |
| Strings | `src/BoundsChecked.cpp`: `strncpy_s`, `strcpy_s`, `strncat_s`, `strcat_s`, `memcpy_s`, `memmove_s`, `memset_s`, `strnstr`. `src/WideConversion.cpp`: C-locale `wcstombs`. `src/MiscExtras.cpp`: `strtoumax`, `asctime`. |
| Formatting | `src/Formatting.cpp`: narrow printf and scanf families (`fscanf` added). `src/FormattingWide.cpp`: `vswprintf`, `wprintf`, `fputwc`, `fputws` (UTF-16 guest `wchar_t`, UTF-8 on byte streams). `src/FormattingChecked.cpp`: `sprintf_s`, `snprintf_s`, `vsprintf_s`, `printf_s`. `include/ScanfArguments.hpp`: System V va_list marshalling for scanf on Windows. |
| Stdio extras | `src/StdioExtras.cpp`: `fopen_s`, `fgetpos`, `fsetpos`. |
| Lifecycle | `src/RuntimeSupport.cpp`: `__cxa_atexit`/`cxa_atexit`, `cxa_finalize`, `init_env`, 4-byte atomics; `src/CxxAbiSupport.cpp`: thread-exit destructors. `libSceLibcInternal` forwards `__cxa_finalize` to the same registry. |

## Decision

Port the AnyPS5 libc additions that are general mechanisms, adapting each to this repository's rules instead of copying it:

- Errors a real console reports as codes are codes (nothrow `new` returns null, `fopen_s`/`fgetpos` return errno values, formatting failures are `-1`/`EINVAL`). Only genuinely unsupported states abort through `Unsupported()` (throwing `operator new` without a `new_handler` on an exhausted heap, `reallocalign` of an existing block).
- Nothing new throws a host exception into guest frames: the shared unwinder lets a guest `catch (...)` swallow it, and host typeinfo never matches a guest `catch (std::bad_alloc&)`.
- Guest errno (`__error()`, FreeBSD numbering) is set, never the host `errno`.
- Guest callbacks (exit handlers, thread-exit destructors) are stored and called as System V (`APS5_VABI`) functions; a plain function pointer passes the argument in the wrong register on Windows.
- The guest data model is LP64; the Windows host is LLP64. scanf length modifiers `l`, `z`, `j`, `t` on integer conversions are rewritten to `ll` so a guest `long` receives 8 bytes.

Not ported (see the PR for the per-commit reasoning): the `PreciseWait.hpp` timing helper (unused, belongs to the kernel sync lane), the removal of libc exports a particular title replaces with its own `libc.prx`, the C11 thread layer helpers (`_Throw_C_error`, `_Random_device`, `_Lockfilelock`), and the char locale facets that PR 48 covers.

## Target design

**Mspace.** A handle is the region base. `Arena` owns `chunks` (start to `{end, used, requested}`, tiling the region without gaps) and `free` (ordered `(size, start)` for best fit). Every boundary is 16-byte aligned. Requests round up to 16 bytes with an overflow check (`RoundRequest`); a request whose rounding would wrap `size_t` fails with `ENOMEM` instead of shrinking to a tiny size. One global mutex guards all arenas; the thread-unsafe creation flag is accepted and ignored.

**Wide formatting.** `FormatWide` parses the UTF-16 format and reads arguments from the guest System V va_list (`LibcDetail::FormatArguments`). `%s` is narrow UTF-8, `%ls`/`%S` is UTF-16, `%n` is rejected. Field widths and precisions above 65536 are rejected (`-1`/`EINVAL`): the host snprintf sizes scratch space from the width on the stack.

**Scanf on Windows.** `ScanfArguments` walks the format once, pulls one pointer per argument-consuming conversion out of the System V va_list, rewrites length modifiers, and the caller invokes the host scanf with the pointers as ordinary varargs. At most 16 conversions per call; more is `EOF`/`EINVAL`.

**Exit handlers.** `__cxa_atexit` appends `{func, arg, dso}` under `g_exitMutex`; the first registration installs a `std::atexit` hook that finalizes everything. `CxaFinalize_nid_no_patch(dso)` runs matching handlers newest first with the lock released (handlers may register more); a null `dso` runs all.

## Interfaces

Every export is `extern "C"`, `APS5_VABI`, named `<name>_nid_postfix`, with a doc comment stating parameters and return/errno values (enforced by `tools/check_comments.py`). Cross-prx host API: `CxaFinalize_nid_no_patch(void*)` (declared in `include/General.hpp`).

## Failure modes

| Condition | Result |
|---|---|
| Mspace request rounds past `SIZE_MAX`, or no free chunk fits | `nullptr`, errno `ENOMEM` (12) |
| Unknown mspace handle, foreign or double-freed pointer, bad alignment | `nullptr`/`-1`/code, errno `EINVAL` (22) |
| `operator new` (throwing) on an exhausted heap, no `new_handler` | logged abort via `Unsupported` |
| nothrow `new`, `aligned_alloc`, `strndup`, `asprintf` on an exhausted heap | `nullptr`/`-1`, errno `ENOMEM` |
| `_s` function constraint violation | `EINVAL` (22) / `ERANGE` (34), destination neutralized |
| `wcstombs` unit above 0xFF | `(size_t)-1`, errno `EILSEQ` (86) |
| Wide or scanf format rejected | `-1`/`EOF`, errno `EINVAL` |
| Null, closed or unwritable stream for `fputwc`/`fputws` | `-1`, errno `EINVAL`/`EIO` |

## Tests

GoogleTest, label `unit`, no game data: `mspace_tests` (contract, overflow regression, nested mspaces, randomized stress against a model, concurrency), `guest_lifecycle_tests`, `guest_wide_io_tests` (includes the Windows `sscanf` regression), `guest_libc_extras_tests`, `guest_heap_frontend_tests` (over a deterministic test allocator, death tests for the abort paths), `application_heap_default_tests`.

## Milestones

- [x] Ordered mspace allocator with memalign, reallocalign, stats and nested mspaces.
- [x] Exit handler registry with dso handles and guest calling convention.
- [x] Wide formatting, wide stream output, scanf on all hosts.
- [x] Bounds-checked strings, allocating strings, operator new/delete family.
- [ ] `reallocalign` of an existing block (needs an allocator usable-size query).
- [x] Width/precision cap in the narrow Windows formatter (`WindowsFormatting.hpp`).

## Open questions

- When the replacement table pointer itself is null (not just all-empty), should the application heap fall back to the guest heap? Only the all-empty table is handled today.
- Sony's exact constraint-handler behaviour for the `_s` printf variants is not modelled; they follow host snprintf truncation.
- The Windows host scanf accepts a short `%Nc` field (`sscanf("hello, world
", "%8c%8c")` returns 2, C and FreeBSD return 1). Fixing it needs an own scanf engine; not done.
