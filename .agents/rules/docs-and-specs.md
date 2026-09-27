# Docs and specs

- `docs/PRD.md` holds goals and scope. `docs/ROADMAP.md` holds the milestones. `docs/spec/<subsystem>.md` holds the design.
- If a change alters a subsystem's behaviour, interface or decision, update that spec's Current state, Target design or Decision in the same PR. Use the `update-spec` skill.
- Every spec uses the same sections: Scope, Current state, Decision, Target design, Interfaces, Failure modes, Tests, Milestones, Open questions.
- A file reference names its tree (AnyPS5 `main`, PR #5, or PortPS5) and gives `path:line`. Verify it before citing.
- Hardware is described only as the generic reference tier (PRD §4.4). Never name a specific personal machine, CPU or GPU model owned by a contributor.
- The scope decisions in the PRD are settled. Don't re-open them in specs; propose changes in an issue instead.
- Write directly: no filler, and tables where they help.
