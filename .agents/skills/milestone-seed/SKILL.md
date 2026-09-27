---
name: milestone-seed
description: Turn a PortPS5 ROADMAP milestone into an executable work plan (an Ouroboros seed YAML under seeds/, or a task plan for tools without Ouroboros). Use when starting work on a milestone M0-M6.
---

# Milestone seed

1. **Read** the milestone in `docs/ROADMAP.md` (scope and exit criteria) and every spec file it touches (`docs/spec/*.md` → Milestones sections).
2. **Draft `seeds/m<N>-<slug>.yaml`** with these fields:
   - `goal`: one sentence stating the milestone outcome.
   - `constraints`: the relevant `.agents/rules/*` points, plus the milestone's scope boundaries (what is *not* in it).
   - `acceptance_criteria`: one per exit criterion, as outcomes rather than steps. Each has a single-line, read-only `verify` command. Criteria that can only be checked locally (GPU, game data) say `verify: NONE` and name the results JSON field that proves them.
   - `context_references`: the spec files and code paths.
3. **Check:** every exit criterion in ROADMAP maps to exactly one acceptance criterion, and nothing extra is added.
4. **Run it:**
   - Claude Code with the Ouroboros plugin: `ooo run seeds/m<N>-<slug>.yaml`.
   - Other tools: use the seed as the task plan and work through the acceptance criteria in order.
