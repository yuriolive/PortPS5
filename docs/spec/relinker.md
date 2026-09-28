# PortPS5 — Spec: Relinker

Status: draft v2 · 2026-09-27 (M1 relinker item implemented)

## Scope

`core/relinker`: ELF-to-PE conversion of a decrypted guest executable, import (NID) binding, the hand-emitted Windows entry and loader stub, guest TLS, relocations, `.eh_frame` metadata hand-off, instruction discovery (`CodeMap` from `CodeInstructionCollector`), `--to-intel` (`Amd64OnlyConverter`), the `sce_module` guest-module path and the CLI. The export-side NID renaming (`core/libs/nid`, `nid_patcher`) is specified in [build-toolchain.md](build-toolchain.md); this spec owns only the import-side contract. Guest code is never recompiled (the decision table in [README.md](README.md#subsystem-specs) §Execution model).

## Current state

File references are `core/relinker/...` unless marked `libs/` (= `core/libs/`). "main" = `e06dbff`, "PR #5" = `29b4601`.

**M1 implementation** (`feat/m1-relinker-codemap`): `Domain::CodeMap` (`domain/include/domain/CodeMap.hpp`) built once per image by `Relinker::BuildCodeMap` (`relinker/src/analysis/CodeMap.cpp`) from `CodeInstructionCollector::CollectDetailed` (`relinker/include/relinker/analysis/CodeInstructionCollector.hpp`); `Amd64OnlyConverter` matches only at `Starts` with branch checks against `BranchTargets` (`codegen/src/Amd64OnlyConverter.cpp`), register forms leave bytes as `Residual` (`codegen/src/x86/Amd64OnlyInstructionMatcher.cpp`), trampolines emitted by `WindowsTrampolineBuilder` (`elfpatcher/src/windows/WindowsTrampolineBuilder.cpp`) and Linux extra-block stubs (`elfpatcher/src/linux/LinuxElfPatcher.cpp`); conversion report (`relinker/src/output/ConversionReport.cpp`) with NIDs in/out, stubs and residual sites written as `.conversion.json` next to `--registry` output (`main.cpp`).

**Pipeline on main** (`main.cpp:28-129`):

| Step | Code | Notes |
|---|---|---|
| Parse CLI | `cli/src/CliArgs.cpp:8-61` | Flags: `--windows`, `--windows-diagnostics`, `--skip-syscall-check`, `--skip-sce-module`, `--to-intel`, `unused-filter=0\|1\|2`, `--registry`, `--rpath` (default `$ORIGIN/libs`, `Cli.hpp:21`), `--lazy-binding`, `--autorun`. Usage text says `<output.elf>` even for PE (`CliArgs.cpp:56`). |
| AMD-only rewrite | `main.cpp:44-55` | Runs before relinking, on `ElfReader::ReadCodeSegments()`. |
| Relink | `relinker/src/pipeline/RelinkerPipeline.cpp:36-305` | Takes only the **first** `PF_X` `PT_LOAD` as text (`:44-50`); requires `PT_DYNAMIC`; each `DT_OS_*`/`DT_*` pair must have exactly one member (`:81-89`); collects `NidReference`s from RELA and JMPREL (`:164-197`); syscall scan (`:202-203`); NID filter levels (`:213-241`); PLT compaction at level 2 (`:246-252`); copies `R_X86_64_RELATIVE` (`:265-281`); builds the call registry (`:283-302`). |
| Guest modules | `main.cpp:80-82`, `relinker/src/guest/GuestModuleBuilder.cpp` | Converts `sce_module` libraries unless `--skip-sce-module`. Not in PR #5. |
| PE emission | `elfpatcher/src/windows/WindowsPePatcher.cpp:42-100` | `WindowsLoadImage`; `WindowsRelocationBuilder::Apply` (`:47`); `.procpar` from segment type `0x61000001` (`:52-61`); `.ehmeta` holding the `PT_GNU_EH_FRAME` RVA (`:62-68`); TLS directory (`:69`); `.reloc` (`:70-75`); kernel32 imports (`:76-80`); entry stub (`:91`). Image base `0x140000000` (`WindowsPeFormat.hpp:14`); console subsystem (`WindowsPeWriter.cpp:32`). |

**Relocations.** `ValidationPolicy` accepts 14 types (`ValidationPolicy.cpp:30-47`), but `WindowsRelocationBuilder::Apply` only handles `R_X86_64_64`, `GLOB_DAT`, `JUMP_SLOT` and `RELATIVE` (`WindowsRelocationBuilder.cpp:21-22`). Any other accepted type fails late, at PE emission. Overlapping targets are rejected (`:24-27`). Import slots are zeroed and bound at startup (`:45-47`). `ValidateSyscallAbsence` is an empty body (`ValidationPolicy.cpp:56-57`).

**Entry and loader stub** (`WindowsEntryStubBuilder.cpp:33-391`). It emits `.startup` (data) and `.entry` (code). The loader calls `GetModuleFileNameA`, then for each library `LoadLibraryExA` (`:256`), then `GetProcAddress` for each import by NID string (`:295`). With `--lazy-binding`, unresolved imports get lazy stubs (`:300`, `:363`, patched into GOT by `WindowsPePatcher.cpp:97-98`). The platform TLS resolver import is special-cased (`:283-287`). The ELF entry is called at `:333`, then `ExitProcess` (`:338`). The stub's own `UNWIND_INFO` is a raw byte list (`:62`: version 1, 10-byte prolog, 6 codes). This is the "magic bytes" example that the decision table in [README.md](README.md#subsystem-specs) cites as `:60`; line 60 is the function-table reservation just before it.

**NID binding.** The relinker never computes NIDs. It forwards the dynsym name of each import, and `GetProcAddress` matches it against prx exports that `nid_patcher` renamed at build time (`libs/nid/src/NidResolver.cpp:23-66`). `ComputeNid` takes a `libraryName` parameter but does not use it (`libs/nid/src/NidCompute.cpp`), so export names are library-agnostic.

**TLS** (`WindowsTlsBuilder.cpp:42-169`). It supports exactly two FS-relative forms: `mov r64, fs:[0]` and `mov dword fs:[0x28], imm32` (`:79-82`). Any other FS-prefixed instruction throws. Each site is overwritten with `jmp rel32` into `.gtcode` (`:23-38`). A branch landing inside a patched site is rejected (`:133-136`). `.gtls` holds the IMAGE_TLS_DIRECTORY, the template and a 0x30-byte TCB (`:103-107`). The TLS callback handles process attach and thread attach (`:122-131`).

**Instruction discovery. There are two engines on main:**
- `codegen/src/InstructionScanner.cpp:28-37`: linear sweep. Used by `SyscallScanner.cpp:31-53` and by `Amd64OnlyConverter`.
- `relinker/src/analysis/CodeInstructionCollector.cpp:27-223`: recursive descent. It is seeded from the ELF entry, `DT_INIT`/`DT_FINI`, defined FUNC symbols, relocation targets, init/fini arrays and `.eh_frame` FDE ranges (`ReadExceptionFunctions`, `:168-172`). It decodes FDE ranges fully (`:176-192`), iterates `BuildControlFlowGraph` to a fixpoint (`:193-211`), and rejects overlapping instruction starts (`:212-221`). **Only `WindowsTlsBuilder` uses it** (`WindowsTlsBuilder.cpp:48`). the decision table in [README.md](README.md#subsystem-specs)'s "seed from `.eh_frame` and the CFG" therefore already exists; the work is to make it the only engine.

The CFG (`UnusedNidFilter/ControlFlowGraph.cpp:21-107`) stops at indirect jumps (`:99-102`). It resolves `jmp/call [rip+x]` only through relocation-indexed slots (`:68-76`). `CfgBackedNidFilter` (`UnusedNidFilter.cpp:9-37`) keeps a reference only if a reachable instruction touches its GOT slot. The strict filter requires exactly one immutable code segment (`StrictNidFilter.cpp:54-58`).

**`--to-intel`.** On main, `Amd64OnlyConverter.cpp:41-63` shifts later offsets when a replacement is longer and truncates at `:63`. The substitution table maps each of MONITORX, MWAITX, CLZERO, RDPRU and MCOMMIT to its own encoding (`Amd64OnlySubstitutionTable.hpp:20-24`), so the canonical form is a no-op. There is no SSE4a handling. PR #5 (`01c4e3e`) changes this:
- `Amd64OnlyConverter.cpp:74-149` classifies each site as `InPlace`, `Trampoline` or `Unsupported`;
- `Unsupported` **throws** (`:139-140`);
- the branch-into-site check (`:124-126`) uses branch targets from the linear sweep (`:56-72`);
- segment size must not change (`:146-147`).

The PR #5 matcher (`Amd64OnlyInstructionMatcher.cpp`):
- lowers MOVNTSS/MOVNTSD in place to MOVSS/MOVSD stores (`:34-45`);
- sends EXTRQ/INSERTQ to `Sse4aLowering` (in place when the sequence fits, `Sse4aLowering.cpp:125-158`, otherwise out of line, `:160+`);
- marks register forms and the MONITORX family `Unsupported` (`:47-50`, `:75-88`).

`WindowsTrampolineBuilder.cpp:25-56` appends `.amdstub`. It verifies the original bytes (`:37-38`) and the rel32 range (`:45-46`). A runtime EXTRQ/INSERTQ trap exists only in PR #5 (`libs/prx/libc/src/specifics/windows/CrashReport.cpp:168-172`), behind `APS5_NO_SSE4A_EMULATION` and `APS5_TRACE_SSE4A`. PR #5's own TechnicalDebt.md states that register forms fail the relink.

## Decision

- **Keep** main's relinker as the base: pipeline, `sce_module` guest modules, TLS, loader stub and `CodeInstructionCollector`. PR #5 predates these.
- **Adopt** PR #5's `--to-intel`: `Sse4aLowering`, `Sse4aOperands`, `WindowsTrampolineBuilder` and its tests, re-based onto the new code map below.
- **Replace** linear sweep everywhere with one `CodeMap` built from `CodeInstructionCollector`.
- **Change** PR #5's register-form behaviour from "fail the relink" to "leave the bytes and rely on the runtime trap". This implements the decision table in [README.md](README.md#subsystem-specs) §Relinker. The trap moves to libc with no `APS5_*` switch; tracing goes to `[debug]` ([configuration.md](configuration.md)).

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
| `Amd64OnlyConverter` | linear sweep (PR #5) | Match only at `Starts`. The branch-into-site check uses `BranchTargets`. An AMD-only pattern in `Unproven` is logged, not patched. |
| `WindowsTlsBuilder` | collector | Unchanged, but reads the shared `CodeMap`. |
| `CfgBackedNidFilter` | own CFG run | Reuses `CodeMap` reachability. |

**Where `--to-intel` runs.** It moves after `ElfReader` and before `RelinkerPipeline::Relink`, on the same `CodeMap`, and sets `Trampolines` for `WindowsPePatcher`. For each site:

```
for site in CodeMap.Starts where matcher(site) != none:
  if IsTarget(x) for x in (site, site+len): error "branch enters AMD-only site"
  InPlace     -> rewrite, same length (assert)
  Trampoline  -> len >= 5 required; record TrampolineSite
  RegisterForm (EXTRQ/INSERTQ 0F 79 / F2 0F 79) -> leave bytes; add to Residual
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
- libc runtime (see [threading.md](threading.md)): the ported SSE4a trap handles `Residual` sites, and the `.ehmeta` and `.ehfram` sections feed the DWARF unwinder (`libs/prx/libc/src/exception/Unwind.cpp:158,179`).
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
| EXTRQ/INSERTQ register form | Leave the bytes, list them in the report, rely on the runtime trap. |
| `Unproven` bytes | Count and log them. Never patch them and never fail on them. |
| Missing prx export at startup | The loader prints `FAIL: unresolved ELF import <nid>` (`WindowsEntryStubBuilder.cpp:99`). Without `--lazy-binding`, it stops. |
| CodeMap overlap | Fail (`CodeInstructionCollector.cpp:214`). This means data was treated as code, and the seeding is wrong. |

## Tests

- **GoogleTest Unit Suites & Unit Tests** (`ctest -L unit`, hosted `unit` job):
  - Legacy tests migrated to GoogleTest: `strict_nid_filter`, `optional_plt`, `empty_tls`, `tls_function_coverage` (Python), `windows_dependency_diagnostics`. From PR #5 (ported, general mechanisms only): `amd64_only_converter` (`codegen/tests/Amd64OnlyConverterTests.cpp`), `amd64_only_windows` (`elfpatcher/tests/Amd64OnlyWindowsTests.cpp`, PE builder only, no libc dep).
  - New unit tests on synthetic ELFs: `codemap` (`relinker/tests/CodeMapTests.cpp`):
    - Jump table and literal pool inside `.text`: linear sweep desyncs, `CodeMap` does not.
    - SSE4a register form: relink succeeds and site appears in `Residual`.
    - Branch into a stub site: expected failure.
    - Relocation-table consistency: every type `ValidationPolicy` accepts is emitted by the builder.
    - Golden bytes for `.startup`/`.entry` and the `UNWIND_INFO` block.
    - Libc trap: call the SSE4a emulator directly on a synthetic `CONTEXT`, host-CPU independent.
- **Ported Ecosystem Test Suites:**
  - **Wine / Proton PE Construction Patterns:** PE base relocation table generation, section header alignment rules, and export directory table formatting.
- **Local regression** ([verification.md](verification.md) §2): each gate title converts with `--to-intel` and without, and the conversion report is recorded.

## Milestones

- [x] **M0:** rebase keeps main's relinker. Existing relinker tests are wired into `ctest`. Stub magic bytes get "why" comments (CONVENTIONS change).
- [x] **M1:** `CodeMap` built from `CodeInstructionCollector` as the only instruction-discovery engine (ROADMAP M1 relinker item), which `--to-intel` depends on; `--to-intel` port from PR #5, register-form fallback plus the libc trap without `APS5_*`; the `APS5_EXPORT_FN` export macro (the `policy` job); and the conversion report used by the "inventory each gate title's imports" item.
- [ ] **M2–M5:** no planned relinker scope. Fixes are driven by gate-title conversion failures.
- [ ] **M6:** CLI usage section of the user guide.

## Open questions

1. MONITORX, MWAITX, CLZERO, RDPRU and MCOMMIT: fail conversion (PR #5), or trap and emulate at runtime as no-ops or fences?
2. Support several executable segments, or keep rejecting them? This needs gate-title evidence from M0 dumps.
3. Should a syscall pattern in `Unproven` bytes stay a warning if a gate title's `Unproven` share turns out large?
4. Which default `unused-filter` level is safe for 1.0? Level 2 rewrites the PLT (`PltCompactor`).
5. Keep the Linux ELF output path (`LinuxElfPatcher`) building in CI, although other platforms are post-1.0?
