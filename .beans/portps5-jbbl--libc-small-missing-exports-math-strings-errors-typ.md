---
# portps5-jbbl
title: 'libc: small missing exports (math, strings, errors, typeinfo, stdio)'
status: todo
type: task
created_at: 2026-10-01T21:42:54Z
updated_at: 2026-10-01T21:42:54Z
parent: portps5-dbpx
---

## Context

Port from AnyPS5 (9): `rand`, `tanh` (`src/Math.cpp`), `wcslen`, `wmemset`, `_Stoull` (`src/Strings.cpp`), `_Assert` (`src/StdioExtras.cpp`), `_ZSt14_Random_devicev`, `_ZSt14_Throw_C_errori` (`src/RuntimeSupport.cpp`), `catchReturnFromMain` (`src/Process.cpp:128`, stubbed here). No reference: `_ZSt16_Throw_Cpp_errori`, `_ZNSt14overflow_errorD1Ev`, `_ZTVSt14overflow_error`, `_ZTVN10__cxxabiv119__pointer_type_infoE`, `_ZTVN10__cxxabiv120__function_type_infoE`, `fdopen`, `localeconv`, `sceLibcMspaceAlignedAlloc`.

Source: NID gap analysis of a local, non-gate dump (PPSA04489), matched against built PortPS5 prx export tables at main@5dd65fe3 and the reference trees AnyPS5 709d7fe, KytyPS5 4428640, SharpEmu e007d43 and shadPS4 fecfbed0. Only NIDs and function names were read from the dump. 'Imported' does not mean 'called': confirm against the gate-title inventories (portps5-zadg, portps5-3eh1) before porting. Licences: AnyPS5 and KytyPS5 GPL-2.0, shadPS4 GPL-2.0-or-later (port with the file's copyright header and cite the commit); SharpEmu is C#, so it is a behavioural reference only.

Blocked by: none.

## Higher Goal

No libc import of a converted title resolves to a stub or fails to link.

## Acceptance Criteria

- [ ] The 9 AnyPS5 functions ported with tests
- [ ] Typeinfo vtables and overflow_error match the guest libc++ ABI; a test throws and catches across the boundary
- [ ] `fdopen`, `localeconv` (classic C locale, as LocaleSupport does) and `sceLibcMspaceAlignedAlloc` implemented with POSIX/SCE codes
- [ ] `tools/check_prx_imports.py` and the import inventory show no unresolved libc NID for the gate titles

## Out of Scope

Non-C locales.
