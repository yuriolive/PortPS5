---
# portps5-8r22
title: 'Dreaming Sarah boot blocked: verbatim-export Config::Loader API from libc.prx'
status: completed
type: task
priority: high
tags:
    - beads:portps5-52
created_at: 2026-09-30T22:53:50Z
updated_at: 2026-09-30T22:53:50Z
---

## Description

Boot of converted PPSA02929 PE dies at prx load: libSceVideoOut imports verbatim _ZN7PortPS56Config6Loader13IsInitializedEv but nid_patcher hashed the export (GetLastError 127). Post-FAIL exit is the deliberate RaiseException(0xC0000135) from WindowsDependencyStubBuilder.cpp:74-82 (verified, not an AV). Fix: _nid_no_patch free-function wrappers for Loader::IsInitialized + Loader::Get in core/libs/prx/libc/src/Config.cpp (call sites use the suffixed names, per the ResolvePath_nid_no_patch neighbour pattern), update 6 call sites (Ajm.cpp, AudioOut2Context.cpp, PadInput.cpp), extend config_tests. See docs/spec/build-toolchain.md NID rule and docs/spec/relinker.md Failure modes.

## Acceptance Criteria

libSceVideoOut.prx loads; no 127; config_tests green; boot reaches next fatal

Migrated from beads `portps5-52`.
