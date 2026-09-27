# CLAUDE.md

Claude Code entry point. The canonical instructions are shared with opencode and Antigravity, so edit those, not this file.

@AGENTS.md
@.agents/rules/legal-boundary.md
@.agents/rules/no-title-hacks.md
@.agents/rules/cpp-style.md
@.agents/rules/testing.md
@.agents/rules/docs-and-specs.md
@.agents/rules/git-workflow.md

Skills: `.claude/skills` is a symlink to `.agents/skills`. On Windows, clone with `git -c core.symlinks=true clone ...` and have Developer Mode enabled, or the link checks out as a plain text file.

## Claude-only: Ouroboros (`ooo`)

When the Ouroboros plugin is installed:

- `ooo seed` turns a milestone into `seeds/m<N>-*.yaml` (see the `milestone-seed` skill).
- `ooo run <seed>` executes it.
- `ooo evaluate` checks the result.

Without the plugin, use the seed YAML as a plain task plan.
