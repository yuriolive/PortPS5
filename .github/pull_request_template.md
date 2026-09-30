## Context

<!-- Root problem, motivation, and current state. -->

## Higher Goal

<!-- Architectural intent, systemic benefits, and why this matters. -->

## Acceptance Criteria

<!-- Concrete, verifiable deliverables using checkboxes. -->
- [ ] 

## Out of Scope

<!-- Explicit boundaries, deferred items, and non-goals. -->

## Summary of Changes

<!-- Specific files, implementations, tests, or docs modified. -->

## Roadmap & Specs

- **Milestone:** <!-- Milestone M0–M6 (see docs/ROADMAP.md), or N/A (e.g. chore) -->
- **Specs touched/updated:** <!-- e.g. docs/spec/build-toolchain.md, or None -->

## Upstream Attribution

<!-- If porting or adapting from AnyPS5 main or a PR, cite the source commit hash and PR number. -->
- Upstream source commit/PR: <!-- e.g. AnyPS5 commit e06dbff or PR #5 / N/A -->

## Verification & Testing

<!-- List the tests added or executed and how verification was performed (e.g. ctest, CI preset, local tool). -->
- Tests run:
- Results:

## PR Checklist

- [ ] **Legal boundary:** No Sony keys, firmware, SDK headers, decryption code, game dumps, shader bytecode, saves, or game footage attached or committed ([rules/legal-boundary.md](.agents/rules/legal-boundary.md)).
- [ ] **Beans:** Bean(s) solved by this PR are ticked, marked `completed` and archived (`beans archive`) in this PR. Bean IDs: <!-- e.g. portps5-w3s8, or N/A -->
- [ ] **No title hacks in core:** No title-specific branches or title-ID checks in `core/`; game-specific options live strictly in `config/games/<titleId>.toml` ([rules/no-title-hacks.md](.agents/rules/no-title-hacks.md)).
- [ ] **ABI conventions:** Host exports called by guest code use `APS5_VABI` and `noexcept` ([rules/cpp-style.md](.agents/rules/cpp-style.md)).
- [ ] **Specs in sync:** If subsystem behaviour, interfaces, or decisions changed, the corresponding specs under `docs/spec/` are updated in this PR ([rules/docs-and-specs.md](.agents/rules/docs-and-specs.md)).
- [ ] **Hardware references:** Hardware is described strictly as the generic reference tier (PRD §4.4); no personal hardware names or personal specs ([rules/docs-and-specs.md](.agents/rules/docs-and-specs.md)).
- [ ] **Conventional commits:** Commits follow Conventional Commits format with subject <= 72 characters and an explanation of why ([rules/git-workflow.md](.agents/rules/git-workflow.md)).
- [ ] **GoogleTest framework:** All new or ported C++ tests use GoogleTest (`portps5_add_gtest`, `TEST`/`TEST_F`, `EXPECT_*`/`ASSERT_*`); no ad-hoc `abort()`, custom `Require()`, or manual `main()` functions ([rules/testing.md](.agents/rules/testing.md)).
