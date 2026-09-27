---
name: update-spec
description: Keep PortPS5 docs (PRD, ROADMAP, docs/spec/*.md) in sync with a code or design change and run the docs acceptance checks. Use when changing subsystem behaviour, interfaces or decisions, or when editing docs.
---

# Update a spec

1. **Find the spec(s)** for the subsystem using the table in `docs/spec/README.md`.
2. **Edit** only the sections the change affects:
   - Current state: what the code does now, with `path:line` refs checked against the tree.
   - Decision, if it changed. A changed decision needs maintainer approval, so say so in the PR.
   - Target design, Interfaces, Failure modes, Tests.
   - Milestones: keep them consistent with `docs/ROADMAP.md`.
3. **Cross-doc consistency:**
   - If a milestone's scope moves, update `docs/ROADMAP.md`, including its traceability table.
   - If a 1.0 requirement changes, update `docs/PRD.md`.
4. **Hardware wording:** only the generic reference tier. Check with `grep -rniE '(RTX|RX) ?[0-9]{4}|Ryzen [0-9]|Core i[3579]|my PC|your PC' docs`, which must print nothing.
5. **Structure check:** every `docs/spec/<subsystem>.md` except `README.md` and `verification.md` contains the headings Scope, Current state, Decision, Target design, Interfaces, Failure modes, Tests, Milestones, Open questions:
   `for f in docs/spec/*.md; do case $f in *README.md|*verification.md) continue;; esac; for h in Scope 'Current state' Decision 'Target design' Interfaces 'Failure modes' Tests Milestones 'Open questions'; do grep -q "^## $h" "$f" || echo "$f missing $h"; done; done`
