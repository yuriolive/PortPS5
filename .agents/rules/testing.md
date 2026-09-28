# Testing

The protocol is in `docs/spec/verification.md`.

- **Hosted CI (GitHub Actions)** has no GPU and must never need game data.
  - A new test there is a unit test, a relinker test, a golden SPIR-V test on synthetic shaders, or a small driver test on lavapipe.
- **Local-only checks** run on a maintainer's GPU machine with their own dumps.
  - They cover boot, save-checkpoint replay, frame comparisons and full runs.
  - Their only output is the results JSON (use the `compat-result` skill).
- **Every bug fix, issue, or review finding** adds a test that fails without the fix, wherever it can run without game data. Every review comment, edge case, or bug report is a test candidate: never resolve a review finding or issue without adding a corresponding regression unit test.
- **Every new or changed PS5 library function** gets a unit test for its return codes and edge cases.
- **Standard framework is GoogleTest (GTest + GMock).**
  - **Mandatory for all C++ tests:** All new or ported C++ tests (unit, integration, regression, ecosystem ports) MUST use GoogleTest (`GTest::gtest` + `GTest::gmock`). Never use bare `abort()`, custom `Require()`, or manual `main()` functions in C++ tests.
  - Use standard GoogleTest assertion macros: `EXPECT_EQ`, `EXPECT_NE`, `ASSERT_TRUE`, `EXPECT_FLOAT_EQ`, `EXPECT_NEAR`, etc.
  - Verify fatal error paths (`APS5_ABORT`) using GoogleTest death tests (`EXPECT_DEATH`).
- **Tests are wired into `ctest`.**
  - Always use `portps5_add_gtest` (defined in `cmake/PortPS5GTest.cmake`) for C++ test targets. It links `GTest::gtest_main`, handles Windows SEH/DWARF unwinding, marks targets with label `unit`, and wires `gtest_discover_tests`.
  - Only use `portps5_add_test` for non-C++ test scripts (e.g. Python scripts). Don't add targets that only build under `EXCLUDE_FROM_ALL`.
- **Never** weaken or delete an assertion to make a test pass without explaining why in the PR.
- **Performance claims** (fps, ms per frame) need a before/after measurement taken with the same build flags and the same run protocol.
