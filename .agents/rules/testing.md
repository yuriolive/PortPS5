# Testing

The protocol is in `docs/spec/verification.md`.

- **Hosted CI (GitHub Actions)** has no GPU and must never need game data.
  - A new test there is a unit test, a relinker test, a golden SPIR-V test on synthetic shaders, or a small driver test on lavapipe.
- **Local-only checks** run on a maintainer's GPU machine with their own dumps.
  - They cover boot, save-checkpoint replay, frame comparisons and full runs.
  - Their only output is the results JSON (use the `compat-result` skill).
- **Every bug fix** adds a test that fails without the fix, wherever it can run without game data.
- **Every new or changed PS5 library function** gets a unit test for its return codes and edge cases.
- **Standard framework is GoogleTest (GTest + GMock).**
  - Use `EXPECT_EQ`, `EXPECT_NE`, `ASSERT_TRUE`, etc. Never use bare `abort()` or custom unformatted `Require()` in new tests.
  - Verify fatal error paths (`APS5_ABORT`) using GoogleTest death tests (`EXPECT_DEATH`).
- **Tests are wired into `ctest`.** Use `portps5_add_test` or `gtest_discover_tests`. Don't add targets that only build under `EXCLUDE_FROM_ALL`.
- **Never** weaken or delete an assertion to make a test pass without explaining why in the PR.
- **Performance claims** (fps, ms per frame) need a before/after measurement taken with the same build flags and the same run protocol.
