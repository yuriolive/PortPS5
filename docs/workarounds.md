# PortPS5 per-title workarounds

No `[workarounds]` keys are registered. Every gate title currently runs on
general mechanisms only.

## Rules (docs/spec/configuration.md)

- A key names the toggled **mechanism** in engine-neutral words, never a game
  (for example `copy_kernel_linear_match`). The `policy` CI job fails on title
  names and title-ID patterns in key names.
- Each entry below lists the key, its type and default (always the general
  behaviour), the mechanism, why no general fix exists yet, the title IDs and
  patch pins that set it, the tracking issue, and the removal condition.
- Entries use one table row per key, `| \`key\` | type, default | mechanism and tracking |`,
  because the `policy` job parses exactly that row shape. The registered keys
  (via `PORTPS5_WORKAROUND` in code) and this file must match in both
  directions; the `policy` job enforces that.
- A key that no gate title uses at a milestone exit is deleted.

## Keys

(none)
