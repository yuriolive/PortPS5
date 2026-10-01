---
# portps5-zroo
title: 'libkernel: partially resident texture apertures'
status: todo
type: feature
created_at: 2026-10-01T21:28:55Z
updated_at: 2026-10-01T21:28:55Z
parent: portps5-bxvu
blocked_by:
    - portps5-gkef
---

## Context

`sceKernelSetPrtAperture` is a NotImplemented stub (libkernel/DirectMemory/Export.cpp:270, PortPS5 main@5dd65fe3). KytyPS5 src/kernel/memory.cpp implements PRT apertures, which streaming titles such as Demon's Souls use.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Titles that stream textures through PRT apertures map them correctly.

## Acceptance Criteria

- [ ] Aperture registration and query with SCE return codes
- [ ] The driver resolves aperture-backed textures
- [ ] GoogleTest

## Out of Scope

Sparse Vulkan resources.
