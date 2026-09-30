# Docs and specs

- `docs/PRD.md` holds goals and scope. `docs/ROADMAP.md` holds the milestones. `docs/spec/<subsystem>.md` holds the design.
- If a change alters a subsystem's behaviour, interface or decision, update that spec's Current state, Target design or Decision in the same PR. Use the `update-spec` skill.
- Every spec uses the same sections: Scope, Current state, Decision, Target design, Interfaces, Failure modes, Tests, Milestones, Open questions.
- A file reference names its tree and commit (AnyPS5 `main@<sha>`, or PortPS5) and gives `path:line`. A merged upstream PR is cited as the `main` commit that contains it, not as a separate tree. Verify it before citing.
- Hardware is described only as the generic reference tier (PRD §4.4). Never name a specific personal machine, CPU or GPU model owned by a contributor.
- The scope decisions in the PRD are settled. Don't re-open them in specs; propose changes in an issue instead.
- Track deliverables with task checkboxes (`- [ ]` / `- [x]`) in `docs/ROADMAP.md` and `docs/spec/*.md`. When a milestone or spec task is completed, mark its checkbox as finished (`- [x]`) in the same PR.
- **Docstring and Doxygen separation:**
  - Header files (`.hpp` / `.h`): Provide formal public API documentation with `@brief`, `@param`, and `@return` tags.
  - Implementation files (`.cpp`): Provide rich multi-line Javadoc comments (`/** ... */`) describing internal mechanisms, invariants, and ABI conventions (`APS5_VABI`) without duplicating `@param` tags (avoiding Doxygen `WARN_IF_DOC_ERROR` merge collisions).
- Write directly: no filler, and tables where they help.
