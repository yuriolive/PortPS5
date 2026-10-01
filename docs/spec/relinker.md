# PortPS5 — Spec: Relinker

Status: draft v2 · 2026-09-30 (M1 relinker item implemented; AnyPS5 relinker hardening ported; synced with `main` 2026-10-01)

## Scope

`core/relinker`: ELF-to-PE conversion of a decrypted guest executable, import (NID) binding, the hand-emitted Windows entry and loader stub, guest TLS, relocations, `.eh_frame` metadata hand-off, instruction discovery (`CodeMap` from `CodeInstructionCollector`), `--to-intel` (`Amd64OnlyConverter`), the `sce_module` guest-module path and the CLI. The export-side NID renaming (`core/libs/nid`, `nid_patcher`) is specified in [build-toolchain.md](build-toolchain.md); this spec owns only the import-side contract. Guest code is never recompiled (the decision table in [README.md](README.md#subsystem-specs) §Execution model).

## Current state

File references are `core/relinker/...` unless marked `libs/` (= `core/libs/`). "main@e06dbff" is the pre-merge AnyPS5 main (old baseline). "main@75a8668" is the current AnyPS5 main and includes merged PR #5; its line numbers were re-checked there.

**Status as of 2026-09-30 (PortPS5 `main`).** `CodeMap`, `--to-intel`, the conversion report, the hardening ports from PR #56, the wrap-free `ElfReader` size checks from PR #64 and the register-form and short-site lowering from PR #73 are in. The same `a + b` bounds-wrap hazard remains outside `ElfReader` in `relinker/src/analysis/UnusedNidFilter/EntryPointCollector.cpp:74`, `relinker/src/analysis/UnusedNidFilter/RelativeRelocationIndex.cpp:129` and `relinker/src/guest/GuestImageReader.cpp:65` (all under `core/relinker/`; bean `portps5-ld5w`). Lower-risk `off + relaEntSize <= relaSize` loops are at `relinker/src/pipeline/RelinkerPipeline.cpp:166,284` and `relinker/src/analysis/UnusedNidFilter/RelativeRelocationIndex.cpp:54`. The `0F A8/A9/AA/0F 0F/0F FF` decoder lengths are a known gap (Open question 8, bean `portps5-euov`).

**M1 implementation** (`feat/m1-relinker-codemap`): `Domain::CodeMap` (`domain/include/domain/CodeMap.hpp`) built once per image by `Relinker::BuildCodeMap` (`relinker/src/analysis/CodeMap.cpp`) from `CodeInstructionCollector::CollectDetailed` (`relinker/include/relinker/analysis/CodeInstructionCollector.hpp`); `Amd64OnlyConverter` matches only at `Starts` with branch checks against `BranchTargets` (`codegen/src/Amd64OnlyConverter.cpp`), EXTRQ/INSERTQ register forms are lowered through a stub (`codegen/src/x86/Sse4aLowering.cpp`), extending a 4-byte site over the following straight-line code when needed, and stay `Residual` only when that is unsafe (`codegen/src/Amd64OnlyConverter.cpp`, `_absorbFollowing`), trampolines emitted by `WindowsTrampolineBuilder` (`elfpatcher/src/windows/WindowsTrampolineBuilder.cpp`) and Linux extra-block stubs (`elfpatcher/src/linux/LinuxElfPatcher.cpp`); conversion report (`relinker/src/output/ConversionReport.cpp`) with NIDs in/out, stubs and residual sites written as `.conversion.json` next to `--registry` output (`main.cpp`).

**Register forms and short sites** (PR #73 (merged), AnyPS5 `ff1fa9eb`, `9d110245`, `671b8c62` ported adapted onto the `CodeMap` converter):
- EXTRQ (`66 0F 79 /r`) and INSERTQ (`F2 0F 79 /r`) register forms are lowered out of line with SSE2 only (`Sse4aLowering.cpp`, `_emitExtrqRegisterForm`, `_emitInsertqRegisterForm`). Length and index come from `xmm2[5:0]`/`xmm2[13:8]` (EXTRQ) or `xmm2[69:64]`/`xmm2[77:72]` (INSERTQ); `0` means 64; the ignored control bits are masked. Flags, GPRs, the red zone and every XMM register except the destination are preserved; the destination's architecturally undefined upper quadword is zeroed for INSERTQ and left as computed for EXTRQ. The stubs were checked against the real instructions on an SSE4a host (`Sse4aRegisterForm.NativeInstructionAgreesWithModelAndStub`).
- The 4-byte forms are shorter than the 5-byte `jmp rel32`. `_absorbFollowing` extends the site over the next proven instructions: ordinary ones must be sequential, have no RIP-relative operand and no FS/GS override, and no branch target may land in the moved range; a following EXTRQ/INSERTQ joins the same stub. The stub runs the lowered code first and the moved bytes after it. Anything else leaves the site `Residual` with a logged reason (never silent).
- Guest modules (`sce_module`) now receive `--to-intel` stubs: `GuestImage::Trampolines` feeds `WriteLinux` (stubs in the appended RWX block) and `WriteWindows` (`.amdstub` through `WindowsTrampolineBuilder`). Before this change a module needing any stub failed the relink.
- The runtime trap that the `Residual` fallback and the `Unproven` policy rely on is **not in this tree** (grep of `core/libs` finds only the `debug.relinker.trace_sse4a` config key). See Q9.

**Pipeline on main@e06dbff** (`main.cpp:28-129`):

| Step | Code | Notes |
|---|---|---|
| Parse CLI | `cli/src/CliArgs.cpp:8-61` | Flags: `--windows`, `--windows-diagnostics`, `--windows-gui` (GUI subsystem, requires `--windows`), `--skip-syscall-check`, `--skip-sce-module`, `--to-intel`, `unused-filter=0\|1\|2`, `--registry`, `--rpath` (default `$ORIGIN/libs`, `Cli.hpp:21`), `--lazy-binding`, `--autorun`. Usage text says `<output.elf>` even for PE (`CliArgs.cpp:56`). |
| AMD-only rewrite | `main.cpp:44-55` | Runs before relinking, on `ElfReader::ReadCodeSegments()`. |
| Relink | `relinker/src/pipeline/RelinkerPipeline.cpp:36-305` | Takes only the **first** `PF_X` `PT_LOAD` as text (`:44-50`); requires `PT_DYNAMIC`; each `DT_OS_*`/`DT_*` pair must have exactly one member (`:81-89`); collects `NidReference`s from RELA and JMPREL (`:164-197`); syscall scan (`:202-203`); NID filter levels (`:213-241`); PLT compaction at level 2 (`:246-252`); copies `R_X86_64_RELATIVE` (`:265-281`); builds the call registry (`:283-302`). |
| Guest modules | `main.cpp:80-82`, `relinker/src/guest/GuestModuleBuilder.cpp` | Converts `sce_module` libraries unless `--skip-sce-module`. The PR #5 branch (`29b4601`) predates it; both are present in main@75a8668. |
| PE emission | `elfpatcher/src/windows/WindowsPePatcher.cpp:42-100` | `WindowsLoadImage`; `WindowsRelocationBuilder::Apply` (`:47`); `.procpar` from segment type `0x61000001` (`:52-61`); `.ehmeta` holding the `PT_GNU_EH_FRAME` RVA (`:62-68`); TLS directory (`:69`); `.reloc` (`:70-75`); kernel32 imports (`:76-80`); entry stub (`:91`). Image base `0x140000000` (`WindowsPeFormat.hpp:14`); console subsystem by default, GUI (`IMAGE_SUBSYSTEM_WINDOWS_GUI`) with `--windows-gui` (`WindowsPeWriter.cpp:32`). |

**Relocations.** `ValidationPolicy` accepts 14 types (`ValidationPolicy.cpp:30-47`), but `WindowsRelocationBuilder::Apply` only handles `R_X86_64_64`, `GLOB_DAT`, `JUMP_SLOT` and `RELATIVE` (`WindowsRelocationBuilder.cpp:21-22`). Any other accepted type fails late, at PE emission. Overlapping targets are rejected (`:24-27`). Import slots are zeroed and bound at startup (`:45-47`). `ValidateSyscallAbsence` is an empty body (`ValidationPolicy.cpp:56-57`).

**Entry and loader stub** (`WindowsEntryStubBuilder.cpp:33-391`). It emits `.startup` (data) and `.entry` (code). The loader calls `GetModuleFileNameA`, then for each library `LoadLibraryExA` (`:256`), then `GetProcAddress` for each import by NID string (`:295`). With `--lazy-binding`, unresolved imports get lazy stubs (`:300`, `:363`, patched into GOT by `WindowsPePatcher.cpp` `writeGotStub` as 8-byte preferred-VA (`ImageBase + stubRva`) DIR64 slots, each with its own base-relocation entry; `.reloc` is therefore built last, after the entry stubs). The platform TLS resolver import is special-cased (`:283-287`). The ELF entry is called at `:333`, then `ExitProcess` (`:338`). The stub's own `UNWIND_INFO` is a raw byte list (`:62`: version 1, 10-byte prolog, 6 codes). This is the "magic bytes" example that the decision table in [README.md](README.md#subsystem-specs) cites as `:60`; line 60 is the function-table reservation just before it.

**NID binding.** The relinker never computes NIDs. It forwards the dynsym name of each import, and `GetProcAddress` matches it against prx exports that `nid_patcher` renamed at build time (`libs/nid/src/NidResolver.cpp:23-66`). `ComputeNid` takes a `libraryName` parameter but does not use it (`libs/nid/src/NidCompute.cpp`), so export names are library-agnostic.

**TLS** (`WindowsTlsBuilder.cpp:42-169`). It supports exactly two FS-relative forms: `mov r64, fs:[0]` and `mov dword fs:[0x28], imm32` (`:79-82`). Any other FS-prefixed instruction throws. Each site is overwritten with `jmp rel32` into `.gtcode` (`:23-38`). A branch landing inside a patched site is rejected (`:133-136`). `.gtls` holds the IMAGE_TLS_DIRECTORY, the template and a 0x30-byte TCB (`:103-107`). The TLS callback handles process attach and thread attach (`:122-131`).

**Instruction discovery. There are two engines on main@e06dbff:**
- `codegen/src/InstructionScanner.cpp:28-37`: linear sweep. Used by `SyscallScanner.cpp:31-53` and by `Amd64OnlyConverter`.
- `relinker/src/analysis/CodeInstructionCollector.cpp:27-223`: recursive descent. It is seeded from the ELF entry, `DT_INIT`/`DT_FINI`, defined FUNC symbols, relocation targets, init/fini arrays and `.eh_frame` FDE ranges (`ReadExceptionFunctions`, `:168-172`). It decodes FDE ranges fully (`:176-192`), iterates `BuildControlFlowGraph` to a fixpoint (`:193-211`), and rejects overlapping instruction starts (`:212-221`). **Only `WindowsTlsBuilder` uses it** (`WindowsTlsBuilder.cpp:48`). the decision table in [README.md](README.md#subsystem-specs)'s "seed from `.eh_frame` and the CFG" therefore already exists; the work is to make it the only engine.

The CFG (`UnusedNidFilter/ControlFlowGraph.cpp:21-107`) stops at indirect jumps (`:99-102`). It resolves `jmp/call [rip+x]` only through relocation-indexed slots (`:68-76`). `CfgBackedNidFilter` (`UnusedNidFilter.cpp:9-37`) keeps a reference only if a reachable instruction touches its GOT slot. The strict filter requires exactly one immutable code segment (`StrictNidFilter.cpp:54-58`).

**`--to-intel`.** On main@e06dbff, `Amd64OnlyConverter.cpp:41-63` shifts later offsets when a replacement is longer and truncates at `:63`. The substitution table maps each of MONITORX, MWAITX, CLZERO, RDPRU and MCOMMIT to its own encoding (`Amd64OnlySubstitutionTable.hpp:20-24`), so the canonical form is a no-op. There is no SSE4a handling. AnyPS5 main@75a8668 (merged PR #5, commit `01c4e3e`) changes this:
- `Amd64OnlyConverter.cpp:119-175` classifies each site as `InPlace`, `Trampoline` or `Unsupported`;
- `Unsupported` **throws** (`:173-174`);
- the branch-into-site check (`:158-160`) uses branch targets from the linear sweep (`:59-75`, sweep at `:92`);
- a `Trampoline` site shorter than a 5-byte jump absorbs the following instructions when they can move, otherwise it throws (`:131-156`);
- segment size must not change (`:180-181`).

The main@75a8668 matcher (`Amd64OnlyInstructionMatcher.cpp`):
- lowers MOVNTSS/MOVNTSD in place to MOVSS/MOVSD stores, and throws on a register operand (`:42-53`);
- sends EXTRQ/INSERTQ to `Sse4aLowering` (in place when the sequence fits, `Sse4aLowering.cpp:209-242`, otherwise out of line, `:244` onward; the EXTRQ register form is lowered out of line, `:128-150`);
- marks the INSERTQ register form and the MONITORX family `Unsupported` (`:57-58`, `:109-122`).

`WindowsTrampolineBuilder.cpp:25-56` appends `.amdstub`. It verifies the original bytes (`:37-38`) and the rel32 range (`:45-46`). A runtime EXTRQ/INSERTQ trap exists in main@75a8668 and not in main@e06dbff (`libs/prx/libc/src/specifics/windows/CrashReport.cpp:169-172,220-239`), behind `APS5_NO_SSE4A_EMULATION` and `APS5_TRACE_SSE4A` (`:364-365`). `docs/dev/TechnicalDebt.md:37` on main@75a8668 states that the INSERTQ register form and the MONITORX family fail the relink.

## Decision

- **Keep** main's relinker as the base: pipeline, `sce_module` guest modules, TLS, loader stub and `CodeInstructionCollector`. These exist in main@e06dbff; the PR #5 branch (`29b4601`) predates them.
- **Adopt** AnyPS5 main's (merged PR #5) `--to-intel`: `Sse4aLowering`, `Sse4aOperands`, `WindowsTrampolineBuilder` and its tests, re-based onto the new code map below.
- **Replace** linear sweep everywhere with one `CodeMap` built from `CodeInstructionCollector`.
- **Change** AnyPS5 main's (merged PR #5) register-form behaviour from "fail the relink" to "leave the bytes and rely on the runtime trap". This implements the decision table in [README.md](README.md#subsystem-specs) §Relinker. The trap moves to libc with no `APS5_*` switch; tracing goes to `[debug]` ([configuration.md](configuration.md)).
- **Refine** (PR #73 (merged)): lower the register forms through a stub whenever the site reaches 5 bytes (own bytes plus safely movable successors); use the `Residual` fallback above only for sites that cannot be extended. This is a strict reduction of `Residual` sites, so it narrows rather than replaces the decision above. Whether the remaining fallback should fail the relink instead is Q10.

## Target design

**CodeMap.** One immutable result, computed once per image and shared by every consumer:

```cpp
struct CodeMap {
    std::set<VirtualAddress> Starts;            // proven instruction starts
    std::map<VirtualAddress, VirtualAddress> Functions;   // FDE/symbol ranges [begin,end)
    std::set<VirtualAddress> BranchTargets;     // direct targets + relocation roots
    std::vector<AddressRange> Unproven;         // executable bytes never reached
    bool Contains(VirtualAddress) const; bool IsTarget(VirtualAddress) const;
};
CodeMap BuildCodeMap(const ElfImage&);          // CodeInstructionCollector::Collect + ranges
```

| Consumer | Today | Target |
|---|---|---|
| `SyscallScanner` | linear sweep | Scan `Starts` only. A syscall at a proven start is an error. A syscall-like pair in `Unproven` is only logged with its count. |
| `Amd64OnlyConverter` | linear sweep (main@75a8668) | Match only at `Starts`. The branch-into-site check uses `BranchTargets`. An AMD-only pattern in `Unproven` is logged, not patched. |
| `WindowsTlsBuilder` | collector | Unchanged, but reads the shared `CodeMap`. |
| `CfgBackedNidFilter` | own CFG run | Reuses `CodeMap` reachability. |

**Where `--to-intel` runs.** It moves after `ElfReader` and before `RelinkerPipeline::Relink`, on the same `CodeMap`, and sets `Trampolines` for `WindowsPePatcher`. For each site:

```
for site in CodeMap.Starts where matcher(site) != none:
  if IsTarget(x) for x in (site, site+len): error "branch enters AMD-only site"
  InPlace     -> rewrite, same length (assert)
  Trampoline  -> len >= 5 required; record TrampolineSite
  RegisterForm (EXTRQ/INSERTQ 0F 79 / F2 0F 79) -> Trampoline; a 4-byte site is extended over movable
                 successors (_absorbFollowing); if it cannot be, leave bytes, log why, add to Residual
  MonitorxFamily -> error listing every site (F1 clear error), see Open questions
emit report: {in_place, stubs, residual[] (rva, mnemonic)}
```

**Relocation table.** One `constexpr` table of target-supported relocation types, owned by the elfpatcher and queried by `ValidationPolicy`. Unsupported types then fail during parsing, with file offset and type name.

**Entry stub legibility.** `UNWIND_INFO` and the opcode sequences move behind named builders (`UnwindInfoBuilder{prologSize, codes[]}`, `UWOP_*` enums) and get "why" comments ([build-toolchain.md](build-toolchain.md), CONVENTIONS). The emitted bytes stay identical, checked by a golden test.

**Export ABI guard.** The prx export macro declares the function itself, e.g. `APS5_EXPORT_FN(ret, name, params)` expanding to `extern "C" ret APS5_VABI name params;` plus the NID alias. This makes it impossible to export a symbol without `sysv_abi`. The compile-time type check is an inference to validate at M1: GCC treats `sysv_abi` as part of the function type on x86-64.

**CLI.** Existing flags are kept. The usage text is fixed. Exit codes are fixed: 0 success, 1 usage, 2 conversion error. A one-line summary (NIDs in/out, stubs, residual sites) goes on stdout, and a JSON conversion report goes next to `--registry` output. That report feeds the M1 import inventory.

## Interfaces

- [build-toolchain.md](build-toolchain.md) owns prx export names (`nid_patcher`), the `.ehfram` rename and the `APS5_VABI` macro. The relinker assumes exports are bare NID strings resolvable by `GetProcAddress`.
- [guest-memory.md](guest-memory.md): the image is preferred at `0x140000000`, and `.reloc` covers every `RELATIVE` target, so the image may be rebased. Guest heaps must not assume the image address.
- [threading.md](threading.md): guest TLS lives in `.gtls`. The TCB is 0x30 bytes, and the `fs:[0]` self-pointer is written by the TLS callback on thread attach. Threads created by libkernel must pass through normal Windows thread attach.
- libc runtime (see [threading.md](threading.md)): the SSE4a trap that is meant to handle `Residual` sites (not yet in the tree, Q9), and the `.ehmeta` and `.ehfram` sections feed the DWARF unwinder (`libs/prx/libc/src/exception/Unwind.cpp:158,179`).
- [configuration.md](configuration.md): conversion reads no TOML. Only `debug.relinker.trace_sse4a` affects the runtime trap, and it only traces.
- [shader-recompiler.md](shader-recompiler.md): relink-time shader compilation is a PRD non-goal, and the TechnicalDebt note proposing it is closed as post-1.0.
- [verification.md](verification.md): conversion is step 1 of local regression, and its report hash goes into the results `log_sha256` bundle.

## Failure modes

| Condition | Policy |
|---|---|
| Relocation type outside the table | Fail at parse, with offset and type name. |
| More than one executable `PT_LOAD` | Today silently only the first is used (`RelinkerPipeline.cpp:44-50`). Target: fail with a clear error until this is supported. |
| Unsupported FS-relative TLS form | Fail with file offset (unchanged). |
| Branch into a patched TLS or AMD-only site | Fail with offset (unchanged). |
| EXTRQ/INSERTQ register form | Lower through a stub. A 4-byte site is extended over the next instructions. If a successor is a control transfer, has a RIP-relative operand or an FS/GS override, is not a proven start, is AMD-only without a stub lowering, or a branch targets the moved range, the site stays `Residual`: bytes untouched, reason logged (`... cannot be extended: <reason>; left for the runtime trap`), listed in the report. |
| `--to-intel` stub in a guest module | Same stubs as the executable. A site outside every executable `PT_LOAD`, bytes that no longer match the recorded original, an invalid body or a stub beyond rel32 reach fail the relink with `RelinkerException`. |
| `Unproven` bytes | Count and log them. Never patch them and never fail on them. |
| Missing prx export at startup | The loader prints `FAIL: unresolved ELF import <nid>` (`WindowsEntryStubBuilder.cpp:99`). Without `--lazy-binding`, it stops. Every FAIL path exits via `ExitProcess` with the printed status (e.g. `0xC0000135`, `0xC0000139`), never via exception dispatch, so the process exit code is the status. |
| prx-to-prx host import missing at load | Same `FAIL` shape with `GetLastError` 127, but the importer is a prx and the symbol is a verbatim host name: the provider hashed its export while the importer asks verbatim (observed 2026-09-29: `libSceVideoOut` importing the mangled `Config::Loader::IsInitialized` from `libc.prx` on the Dreaming Sarah boot path; resolved by exporting verbatim `_nid_no_patch` wrappers). After the message the process exits cleanly non-zero via `ExitProcess(0xC0000135)` with no exception dispatch. |
| Truncated ELF header (< 0x40 bytes) | `ElfReader::ReadHeader` fails with `File too small for ELF header`, never a later out-of-bounds message. |
| Untrusted 64-bit file offset near `UINT64_MAX` | `ElfReader` (including `ReadSegment`, `ReadSection`) and `Io::ReadUxx`/`WriteUxx`/`ByteReader` bounds checks are written `offset > size \|\| size - offset < N`, so a wrapped sum cannot pass the check. The access throws out-of-range and leaves the buffer unchanged. `ReadDynamicTags` is deliberately lenient instead: an offset past the end of the file returns no tags, an oversized `FileSize` is clamped to the file, and a trailing partial entry is ignored (covered by `ElfReaderBounds.DynamicTagsHugeFileSizeClamped`). |
| Output write error surfaces only at flush (disk full) | `Io::FileWriter::Write` calls `close()` before checking the stream, so the failure is reported as `Failed to write file: <path>` instead of leaving a truncated output reported as success. |
| Flagless `PT_LOAD` in a Linux guest module (SCE dynlib data segment) | Dropped from the emitted program headers (never mapped on the console); its address range still bounds the appended block. Forcing `PF_R` on it could shadow the preceding RW segment's bss tail. |
| CodeMap overlap | Fail (`CodeInstructionCollector.cpp:214`). This means data was treated as code, and the seeding is wrong. |

## Tests

- **GoogleTest Unit Suites & Unit Tests** (`ctest -L unit`, hosted `unit` job):
  - Legacy tests migrated to GoogleTest: `strict_nid_filter`, `optional_plt`, `empty_tls`, `tls_function_coverage` (Python), `windows_dependency_diagnostics`. Upstream ports (PR #28, fix + test together): `linux_load_alignment` (first PT_LOAD aligns to the largest kept segment alignment, `elfpatcher/src/general/ProgramHeaderLayoutBuilder.cpp:147-151` on AnyPS5 main@75a8668), `windows_gui` (CUI default, GUI with `--windows-gui`, subsystem word at `WindowsPeWriter.cpp:32` on AnyPS5 main@75a8668). From AnyPS5 main (merged PR #5; ported, general mechanisms only): `amd64_only_converter` (`codegen/tests/Amd64OnlyConverterTests.cpp`), `amd64_only_windows` (`elfpatcher/tests/Amd64OnlyWindowsTests.cpp`, PE builder only, no libc dep).
  - New unit tests on synthetic ELFs: `codemap` (`relinker/tests/CodeMapTests.cpp`):
    - Jump table and literal pool inside `.text`: linear sweep desyncs, `CodeMap` does not.
    - SSE4a register form that cannot be extended (here: followed by `ret`): relink succeeds and the site appears in `Residual`.
    - Branch into a stub site: expected failure.
    - `syscallProvenVsUnproven` and `trampolineSiteRecorded` (implemented).
    - Relocation-table consistency: every type `ValidationPolicy` accepts is emitted by the builder (planned, not implemented).
    - Golden bytes for `.startup`/`.entry` and the `UNWIND_INFO` block (planned, not implemented).
    - Libc trap: call the SSE4a emulator directly on a synthetic `CONTEXT`, host-CPU independent (planned, not implemented; the trap itself is open question 9).
  - Hardening regressions ported from AnyPS5 (GoogleTest on synthetic bytes, `portps5_add_gtest`): `relinker_elf_reader_bounds_tests` (full-header check plus `e_phoff` near `UINT64_MAX`), `relinker_buffer_bounds_tests` (wrap-free bounds in `Io::ReadUxx`/`WriteUxx`/`ByteReader`), `relinker_file_writer_tests` (deferred flush failure; the `/dev/full` cases run on Linux only and are skipped elsewhere), `relinker_x64_decoder_emms_tests` (EMMS `0F 77` has no ModRM), `relinker_linux_guest_module_writer_tests` (flagless `PT_LOAD` dropped). `windows_lazy_got` (Python, real ELF-to-PE pipeline): lazy-import GOT slots hold the preferred VA and have DIR64 relocations.
  - `--to-intel` lowering (GoogleTest, synthetic bytes, `portps5_add_gtest`):
    - `relinker_sse4a_lowering_tests` (`codegen/tests/Sse4aRegisterFormTests.cpp`, `Sse4aImmediateFormTests.cpp`, shared `Sse4aExecutionHarness.hpp`): matcher and `MatchSequence` contracts, constant sharing, and **execution** of the lowered EXTRQ/INSERTQ code in a generated harness (all 16 XMM registers, flags and the 128-byte red zone checked) against an architectural model: every defined `(length, index)` of the immediate forms (in-place and stub shapes) and the register forms over representative fields with junk in the ignored control bits. A native-instruction oracle runs on SSE4a hosts and skips elsewhere.
    - `relinker_amd64_only_short_site_tests` (`codegen/tests/Amd64OnlyShortSiteTests.cpp`): extension over a NOP and over a 5-byte successor, adjacent register forms sharing one stub, every refusal hazard (ret, jmp, call, jcc, RIP-relative, FS and GS override, unproven successor, segment end, branch into the moved range, unmovable AMD-only successor), and the `Unproven` policy (bytes counted, never patched).
    - `relinker_windows_short_site_tests` (`elfpatcher/tests/WindowsShortSiteTests.cpp`, Windows only): the recorded site length is honoured by the PE patcher (jump, NOP padding, return after the whole range).
    - `LinuxGuestModuleWriter.TrampolineSitesJumpToExecutableStubs` and `...MalformedTrampolineSitesAreRejected`, plus `guest_intel_trampolines` (Python, real relinker on a synthetic executable and module, Linux and Windows outputs).
- **Ported Ecosystem Test Suites:**
  - **Wine / Proton PE Construction Patterns:** PE base relocation table generation, section header alignment rules, and export directory table formatting.
- **Local regression** ([verification.md](verification.md) §2): each gate title converts with `--to-intel` and without, and the conversion report is recorded.

## Milestones

- [x] **M0:** rebase keeps main's relinker. Existing relinker tests are wired into `ctest`. Stub magic bytes get "why" comments (CONVENTIONS change).
- [ ] **M1:**
  - [x] `CodeMap` built from `CodeInstructionCollector` as the only instruction-discovery engine (ROADMAP M1 relinker item), which `--to-intel` depends on; `--to-intel` port from AnyPS5 main (merged PR #5); register-form and short-site lowering (PR #73); the conversion report used by the "inventory each gate title's imports" item.
  - [ ] The libc SSE4a trap for `Residual` sites (open question 9).
  - [ ] `APS5_EXPORT_FN` adoption: the macro exists (PR #11) but no export uses it yet, and the `policy` step does not check it ([build-toolchain.md](build-toolchain.md)).
- [ ] **M2–M5:** no planned relinker scope. Fixes are driven by gate-title conversion failures.
- [ ] **M6:** CLI usage section of the user guide.

## Open questions

1. MONITORX, MWAITX, CLZERO, RDPRU and MCOMMIT: fail conversion (AnyPS5 main@75a8668), or trap and emulate at runtime as no-ops or fences?
2. Support several executable segments, or keep rejecting them? This needs gate-title evidence from M0 dumps.
3. Should a syscall pattern in `Unproven` bytes stay a warning if a gate title's `Unproven` share turns out large? The first measurement is 67,605 unproven bytes in 7,601 ranges for Dreaming Sarah, with no boot evidence yet (bean `portps5-7s7x`). Related (portps5-55, 67,605 unproven bytes in one gate title): the converter never patches `Unproven` bytes (`Amd64OnlyShortSite.UnprovenBytesAreCountedNeverPatched`), and it does not report whether they contain AMD-only encodings. With no runtime trap in the tree (Q9), an AMD-only instruction that does execute from such a range raises `#UD` on an Intel host. The question "does the title execute any of them" can only be answered by a boot run; an offline diagnostic that counts AMD-only-looking encodings inside `Unproven` ranges (a linear decode of those ranges, informational only, never patched) would bound the risk first. Not implemented here.
4. Which default `unused-filter` level is safe for 1.0? Level 2 rewrites the PLT (`PltCompactor`).
5. Keep the Linux ELF output path (`LinuxElfPatcher`) building in CI, although other platforms are post-1.0?
6. Dreaming Sarah (PPSA02929) conversion inventory, recorded 2026-09-29: 815 relocation refs, 484 unique NIDs, all 484/484 resolve at link time to built patched-prx exports (the 6 missing libc `#T#T` locale/iostream symbols `ctype`/`collate`/`num_put` facet ids, `num_put` vtable, `locale::_Id_cnt`, and `collate<char>` resolve to placeholder data, stubs, and host-backed streams in `libc/src/LocaleSupport.cpp` and `LocaleSupport.hpp`). Link-level truth supersedes the earlier static 467/18 estimate; the delta was `APS5_EXPORT` literals, plain hashed C names, and `vNe1w4diLCs` = NID(`__tls_get_addr`), which needs no prx export (no relocation ref; the TLS resolver import is special-cased). The 6 plus the `Config::Loader` verbatim-export fix resolve the full import inventory at link time. The conversion probe ran with `--skip-sce-module`; the `sce_module` path is untested for this title (bean `portps5-ks96`). Known runtime limitation: facet runtime behavior is bounded to the classic "C" locale; non-standard categories and virtual facet dispatches are stubs and remain an open runtime gap.
7. Unknown-NID identification method: recompute candidate-name hashes with `NidCompute` and intersect with the game registry; operation labels may be cross-checked against independent emulator-derived registries kept strictly outside the repo. Never vendor such a registry: its labels are conventional (it lists `tls_get_addr`/`scetls_get_addr`, neither of which hashes to `vNe1w4diLCs`), so it corroborates operations, never spellings.
8. (Bean `portps5-euov`.) Decoder length gaps found by probing all `0F xx` opcodes against the Intel SDM no-ModRM list (2026-09-30): `0F A8`/`0F A9` (PUSH/POP GS) and `0F AA` (RSM) are decoded as ModRM-bearing, `0F 0F` (3DNow!, AMD-only) and `0F FF` (UD0) lengths differ from hardware. None is emitted by user-mode PS5 code, so they are left as is; fix if a sweep over a gate title desyncs there.
9. The runtime SSE4a trap that this spec, the README decision table and the M1 milestone line refer to is not present in the tree: only the `debug.relinker.trace_sse4a` config key exists. Until it lands, a `Residual` site and any executed `Unproven` AMD-only byte fault on an Intel host. Implement the trap in libc (another lane), or stop describing it as the fallback.
10. Should a short site that cannot be extended fail the relink instead of staying `Residual`? `.agents/rules/no-title-hacks.md` prefers an error to a skipped site; the settled Decision keeps `Residual` for register forms. With Q9 open the fallback has no runtime backstop, so failing would be safer for Intel users. The relink fails today only for unsupported MONITORX-family sites and branches into a stub site.
11. Indirect control flow into a moved range is invisible to `CodeMap.BranchTargets` (direct targets, relocation roots and function starts only), exactly as for the 5-byte-and-longer stub sites. Extension makes the exposed range larger (up to the successor bytes). A jump table or computed target that lands inside would execute the middle of the replaced `jmp`. Mitigation candidates: refuse extension when the moved range contains a jump-table-referenced address, or add table targets to the map.
12. Stub code has no unwind information and no PE exception-table entry. A moved instruction that faults (a load or store through a bad pointer) reports a fault address inside `.amdstub`, which a guest signal or SEH handler cannot map back to the function. Moved instructions are not restricted to non-faulting ones; restricting them to register-only instructions would remove this risk at the cost of leaving more `Residual` sites.
13. Dead branch: `Sse4aLowering::LowerInPlace` for EXTRQ with `index + length == 64` can never fit. The movq (4 bytes) plus psrlq (5 bytes) sequence is longer than any 6 to 7 byte immediate EXTRQ, so it always returns nullopt. Harmless (the stub path handles it and is tested for every field); left in place for the maintainer to remove or rework.
