---
# portps5-okag
title: 'tools: progress.py --exports, a full per-library export denominator'
status: completed
type: task
created_at: 2026-10-01T22:19:52Z
updated_at: 2026-10-01T22:19:52Z
---

## Context

The library count divides by our own declared APS5_VABI functions (2030), so it overstates coverage and is not comparable across projects. ps5rs data/stubs.txt lists about 50k exports in 128 libraries. Blocked by: none.

## Higher Goal

An honest library-coverage number, measured locally against a full export list, without committing third-party data.

## Acceptance Criteria

- [x] `--exports <file>` parses the per-library list, drops `*_nosubmission` libraries and ignores noise lines
- [x] Definition names are normalised like nid_patcher (`_nid_postfix`, `_nid_no_patch_cut`, `_nid_disambig<N>`)
- [x] progress.json gets an `exports` block only when the flag is passed, so CI output is unchanged
- [x] pytest: parser, normalisation and stub count, main wiring
- [x] verification.md progress-report row documents the local-only option

## Out of Scope

Committing the export list. A --nid-db option: progress.py works on names, not NIDs, so a name database adds nothing here; it belongs in the import-inventory tool (portps5-zadg).

## Summary of Changes

tools/progress.py (load_exports, export_name, collect_export_coverage, --exports), tests/tools/test_progress.py (ExportCoverageTests), docs/spec/verification.md. First local run on main: 1079 of 49486 exports, 1079 of 6477 without libSceNpCppWebApi (43009 C++ template exports).
