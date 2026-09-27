# Testing

The protocol is in `docs/spec/verification.md`.

- **Hosted CI (GitHub Actions)** has no GPU and must never need game data.
  - A new test there is a unit test, a relinker test, a golden SPIR-V test on synthetic shaders, or a small driver test on lavapipe.
- **Local-only checks** run on a maintainer's GPU machine with their own dumps.
  - They cover boot, save-checkpoint replay, frame comparisons and full runs.
  - Their only output is the results JSON (use the `compat-result` skill).
- **Every bug fix** adds a test that fails without the fix, wherever it can run without game data.
- **Every new or changed PS5 library function** gets a unit test for its return codes and edge cases.
- **Tests are wired into `ctest`.** Don't add targets that only build under `EXCLUDE_FROM_ALL`.
- **Never** weaken or delete an assertion to make a test pass without explaining why in the PR.
- **Performance claims** (fps, ms per frame) need a before/after measurement taken with the same build flags and the same run protocol.
