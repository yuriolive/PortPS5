---
# portps5-p7to
title: 'NGS2: implement the voice and rack graph'
status: todo
type: feature
created_at: 2026-10-01T21:28:55Z
updated_at: 2026-10-01T21:28:55Z
parent: portps5-dbpx
blocked_by:
    - portps5-3eh1
---

## Context

libSceNgs2 exports on PortPS5 main@5dd65fe3 are Unsupported stubs (27 in libSceNgs2.native/Export.cpp). KytyPS5 ngs2.cpp and shadPS4 implement the voice and rack graph. Which gate titles import it is unknown until portps5-3eh1.

Reference trees (licences checked: AnyPS5 and KytyPS5 GPL-2.0-only, shadPS4 and SharpEmu GPL-2.0-or-later): AnyPS5 709d7fe, KytyPS5 4428640, shadPS4 fecfbed0, SharpEmu e007d43.

## Higher Goal

Titles that mix through NGS2 produce audio.

## Acceptance Criteria

- [ ] Rack and voice lifecycle with SCE return codes
- [ ] Voices mix into the single mixer
- [ ] GoogleTest on synthetic voices

## Out of Scope

Audio3d.
