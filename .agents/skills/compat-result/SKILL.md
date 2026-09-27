---
name: compat-result
description: Record a locally-run PortPS5 title test (regression or full run) as a results JSON for the compatibility list, without leaking game data. Use after running a gate or compat title on a maintainer GPU machine.
---

# Compatibility result

The schema and the pass rules are in `docs/spec/verification.md` §3–4. The performance definitions are in `docs/PRD.md` §4.3.

1. **Collect** from the runtime telemetry log:
   - frame-time stats (average, 1% low, min, stalls);
   - crashes and softlocks, A/V offset max, pipeline creations after warm-up;
   - checkpoint results and the save round-trip result.
2. **Fill in** the title's `id`, `region` and `patch` so they match its pin in the `docs/PRD.md` §4.1 table.
3. **Set `host_tier`** from the recorded benchmark scores, never from a hand-typed hardware name.
4. **Set `result`:**
   - `pass` only if all the PRD §4.3 thresholds hold;
   - otherwise `fail`, with the failing field noted in the PR.
5. **Write** it to `compat/results/<titleId>/<commit>-<run_type>.json` and open a PR.

## Never include

Paths containing the user's name, screenshots, clips, logs, saves or recorded input. Only `log_sha256` refers to the log.
