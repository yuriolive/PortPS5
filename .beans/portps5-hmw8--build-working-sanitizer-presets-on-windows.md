---
# portps5-hmw8
title: 'Build: working sanitizer presets on Windows'
status: todo
type: bug
priority: high
created_at: 2026-10-01T21:35:21Z
updated_at: 2026-10-01T21:35:21Z
parent: portps5-etxc
---

## Context

The `asan` preset adds `-fsanitize=address,undefined`, but MinGW-w64 GCC 15.2 ships no libasan or libubsan: linking fails with `cannot find -lasan` and `cannot find -lubsan` (verified locally). The preset description in CMakePresets.json already says so; build-toolchain.md lists it as a working preset. On Windows ASan needs clang (llvm-mingw) or MSVC, and MSVC cannot express sysv_abi. GCC still supports UBSan in trap mode (`-fsanitize=undefined -fsanitize-trap=all`, which links with no runtime).

Blocked by: none.

## Higher Goal

Memory and UB bugs in host code are caught by a preset that actually runs.

## Acceptance Criteria

- [ ] Replace `asan` with `ubsan-trap` (GCC, `-fsanitize=undefined -fsanitize-trap=all`) and run its unit label locally; decide with data whether it joins the nightly job
- [ ] Spike: an llvm-mingw clang ASan build of the host-only unit tests (relinker, pure libc and telemetry code) as a dev-only tool that never ships, reporting what breaks (sysv_abi exports, the guest arena's VirtualAlloc2 reservations, vectored fault handlers, the DWARF .eh_frame handling) and whether clang is worth adding as a second, test-only CI compiler; any change to the shipped compiler goes to an issue, not this bean
- [ ] build-toolchain.md preset table and CMakePresets.json description agree with what runs
- [ ] build-toolchain.md points to the Linux ASan/UBSan/TSan CI job (portps5-pbj2) for host-portable targets

## Out of Scope

Making clang a supported compiler for shipped binaries (the toolchain decision stays GCC 15.2).
