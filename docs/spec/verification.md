# PortPS5 — Technical Specification: Verification

Status: draft v1 · 2026-09-27

Verification has three layers. Hosted CI has no GPU and never sees game data. Game checks run locally on a maintainer's GPU machine that meets the reference PC tier (PRD §4.4), and only their results are uploaded.

## 1. Hosted CI (GitHub Actions, every PR and every push to `main`)

- **Environment:** Windows runner (pinned `windows-2022`) with the pinned MinGW-w64 GCC 15.2 toolchain, downloaded and verified by checksum.
- **Jobs:**

| Job | Contents |
|---|---|
| build | Full configure and build (relinker, every prx module, tools) from a clean tree. |
| unit | `ctest` over libc, libkernel and relinker tests, including the futex sync tests (Milestone 1). |
| recompiler-golden | Replays serialised shader requests through the recompiler. It diffs SPIR-V against golden files and validates each module with SPIRV-Tools `spirv-val`. The hosted corpus holds only **synthetic or hand-assembled RDNA2 shaders** written for the project, with no game bytecode. Game-derived shader requests are captured into a local-only corpus on the maintainer machine and replayed by local regression (§2). |
| policy | Fails on new title-specific code patterns and stray switches: no `APS5_` string literals and no `getenv` outside the `Config` module (which alone may name the `APS5_` prefix, to warn about stale variables); test sources under `*/tests/` are exempt from the `getenv` check because they verify environment behavior without changing runtime behavior ([configuration.md](configuration.md)); no title-ID literals in `core/` except `*/tests/`, which need realistic IDs as fixture data; no hash-matched kernel tables; the `PORTPS5_WORKAROUND` registry matches `docs/workarounds.md` in both directions with mechanism-only key names. Compliance with the rule that docs describe hardware only as the generic reference tier is enforced via PR review checklist. |

- **Rules:** no self-hosted runner on the public repository, and no game data, dumps or saves in any artifact.

## 2. Local regression (per build, maintainer GPU machine)

`tools/regress` runs, for each gate title:

1. **Boot:** conversion plus launch, reaching the title screen within a timeout.
2. **Checkpoint loads:** it loads each stored save (one per chapter or boss region), then replays about 60 s of recorded input.
3. **Frame checks:** it compares deterministic frames (menus, the first frames of each FMV) against per-title references. References are keyed by GPU vendor and driver major version. A comparison passes at SSIM ≥ 0.99, so small driver-level differences don't fail the check.
4. **Local shader corpus:** it replays the game-derived shader corpus, which never leaves the machine, through the recompiler and `spirv-val`.
5. **Save round-trip:** save, quit, relaunch, load.
6. **Telemetry:** it records frame-time stats, stalls, watchdog events, A/V offset and pipeline-creation counts.

Saves, recorded input, reference frames and the local shader corpus all stay on the maintainer's machine. Only metrics and hashes are published.

## 3. Release full run (per release, manual)

- One complete start-to-credits playthrough per gate title, with a warm pipeline cache and 1920×1080 or higher output.
- Runtime telemetry is always on. It logs frame times, stalls (gaps of more than 1 s between presents) and crashes. The watchdog flags a softlock when there is no present, or no guest thread progress, for more than 30 s. There is no loading-screen exemption (PRD §4.3 definitions).
- A short clip is captured automatically on failure. It contains game footage, so it stays local and is never attached to issues, PRs or results JSON.
- **Pass:** average of at least 30 fps, 1% low of at least 20 fps, and 0 crashes and 0 softlocks (PRD §4.3).

## 4. Results JSON (uploaded, feeds the compatibility list)

Results are uploaded by a local script as a PR to `compat/results/`, or as a release asset. The schema:

```json
{
  "schema": "portps5.results/1",
  "commit": "<git sha>",
  "run_type": "regression | full_run",
  "title": { "id": "PPSA01342", "region": "EU", "patch": "01.004", "name": "Demon's Souls" },
  "host_tier": "upper-mid-tier (derived from bench scores)",
  "bench": { "cpu_cinebench_r23_multi": 0, "gpu_timespy_graphics": 0 },
  "gpu_vendor": "nvidia | amd | intel",
  "driver_version": "<string>",
  "resolution": "1920x1080",
  "pipeline_cache": "warm | cold",
  "fps": { "avg": 41.2, "p1_low": 27.5, "min": 18.0, "stalls": 3 },
  "av_offset_ms_max": 42,
  "audio_underruns_per_10min": 0.2,
  "audio_device": "wasapi-default | none",
  "spirv_compilations": 0,
  "pipeline_creations_after_warmup": 0,
  "warmup_ms": 4200,
  "capture_split": 12,
  "write_faults": 3100,
  "config_sha256": "<hash of the resolved config>",
  "workarounds_set": [ "copy_kernel_linear_match" ],
  "debug_keys_set": [],
  "crashes": 0,
  "softlocks": 0,
  "checkpoints": [ { "name": "boss-2", "result": "pass", "frame_hash_ok": true } ],
  "save_roundtrip": "pass",
  "result": "pass | fail",
  "log_sha256": "<hash>",
  "duration_s": 36000
}
```

- **Pass rule:** `debug_keys_set` is empty, and in warm-cache runs `spirv_compilations` = 0 and `pipeline_creations_after_warmup` = 0. A run that fails any of these reports `result: "fail"`.
- The results JSON carries no personally identifying hardware detail beyond GPU vendor, driver and tier.

## 5. Test Framework Architecture (GoogleTest & GMock)

PortPS5 standardizes on **GoogleTest (GTest) and GMock** for all unit, integration, and subsystem tests, replacing ad-hoc standalone `main()` executables.

- **Unified Test Suites:** Instead of separate executables for every individual test function, tests are grouped into cohesive suite binaries (`kernel_tests`, `libc_tests`, `relinker_tests`, `shader_recompiler_tests`, `audio_tests`) linked against `GTest::gtest` and `GTest::gmock`.
- **Expression Decomposition:** All test assertions use GoogleTest macros (`EXPECT_EQ`, `EXPECT_NE`, `ASSERT_TRUE`, `EXPECT_THAT`) so that failures report file, line, expression, and actual vs expected values (including formatted SCE/errno error codes).
- **Death Testing (`EXPECT_DEATH`):** Fatal errors in `core/` call the logging abort path (`APS5_ABORT`). GoogleTest death tests verify that invalid guest pointers, misaligned addresses, or unsupported parameters abort cleanly with the expected log message without crashing the test runner process.
- **Hardware/System Mocking (`GMock`):** Subsystems depending on external hardware (Vulkan physical devices, SDL2 audio streams, DualSense gamepad endpoints) use GMock classes to simulate device state, timing, and error conditions deterministically.
- **Reference Test Porting & Ecosystem Ingestion:** Over 55,000 lines of subsystem tests from KytyPS5, sandbox security/threading tests from SharpEMU, FreeBSD 12 kernel/libc tests (`kqueue`, `umtx`, `mmap`), Mesa ACO RDNA2 instruction vectors, Wine NTDLL synchronization/memory tests, and shadPS4/RPCS3 container and ATRAC9 media suites are systematically adapted into PortPS5 GoogleTest suites. All Title IDs are scrubbed to generic synthetic constants (e.g. `PPSA00000`) per legal and policy rules ([TESTING.md](../TESTING.md)).
