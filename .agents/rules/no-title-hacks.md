# No title-specific code

Code under `core/` must work the same for every title.

## Forbidden in core subsystems

- Branches on a title ID, executable hash or shader hash.
- Matching a specific kernel by its exact dwords.
- Caps or limits tuned to one game.
- Silently skipping work that fails (a dispatch, a draw, a system call). Log it and return an error, or abort.
- New behaviour-changing environment variables (`APS5_*`, `PORTPS5_*`). Tracing, dumps and profiling go in the typed `[debug]` config.

## Allowed

- A general mechanism, such as recognising a "uniform fill" or "linear copy" pattern in the shader IR, that happens to help one title first.
- A per-title toggle in `config/games/<titleId>.toml` under `[workarounds]`. The key must name the mechanism, not the game. Add an entry to `docs/workarounds.md` listing the mechanism, why it exists, and which titles use it.

If a title can't progress without a hack, stop. Write down the root cause in the relevant `docs/spec/*.md` "Open questions" section and ask the maintainer.
