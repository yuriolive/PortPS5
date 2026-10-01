# PortPS5 — Technical Specification: Verification

Status: draft v1 · 2026-09-27 · synced with `main` 2026-10-01

Verification has three layers. Hosted CI has no GPU and never sees game data. Game checks run locally on a maintainer's GPU machine that meets the reference PC tier (PRD §4.4), and only their results are uploaded.

## 1. Hosted CI (GitHub Actions, every PR and every push to `main`)

- **Environment:** Windows runner (pinned `windows-2022`) with the pinned MinGW-w64 GCC 15.2 toolchain, downloaded and verified by checksum. Shared bootstrap (submodules, toolchain, ccache env) lives in `.github/actions/mingw-setup` so `ci.yml` and `codeql.yml` cannot drift.
- **Change detection:** a fast `changes` job (`ubuntu-latest`, ~15 s) diffs the PR/push and sets `code_changed`. Docs/config-only runs skip both Windows and Python jobs; `docs/workarounds.md` counts as code because the policy check cross-references the `PORTPS5_WORKAROUND` registry.
- **Caching:** ccache key is stable on the toolchain plus build definition (`mingw-gcc15.cmake`, `mingw-gcc15.sha256`, `CMakePresets.json`, `CMakeLists.txt`) with a per-run save suffix; the old per-SHA key could never hit and wrote a fresh 1 GB entry every run. MinGW and submodule caches are keyed on their pins.
- **Build hashes:** after tests, CI prints SHA256 of the shipping binaries (`relinker.exe`, `libSceAgcDriver.prx`, `agc_shader_replay.exe`) to the step summary for provenance. Summary-only: no artifact upload, no game data, missing files warn instead of failing.
- **Jobs:**

| Job | Contents |
|---|---|
| build_and_test | Single Windows job: full configure and build (relinker, every prx module, tools), `ctest --preset ci` plus the test-count gate (the `ci` configure preset sets `PORTPS5_COMPILE_WARNING_AS_ERROR=ON` and `PORTPS5_REQUIRE_FFMPEG=ON` (#88); the `ci` test preset runs label `unit` with 4 jobs and a 300 s per-test timeout, and includes `prx_cross_import_check`, which runs `tools/check_prx_imports.py` over the built patched libs and fails when a prx imports a name its provider does not export (#68)), then the golden corpus incrementally (`ctest --preset golden` and `agc_shader_replay --golden`; the full build already compiled those targets, so no second runner is needed). Ends with the `policy` step, the comment linter, and the SHA256 summary. |
| **driver-lavapipe** | Windows job `driver_lavapipe`: builds only the targets behind the `lavapipe` label (warm ccache restored read-only; Vulkan runtime and Mesa downloads cached per pinned version), installs the Vulkan loader (from LunarG's runtime-components zip) and a pinned Mesa lavapipe, both SHA-256-verified before extraction (CPU Vulkan ICD, registered under `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`, because the loader ignores `VK_DRIVER_FILES` in the elevated runner process), then runs `ctest --preset lavapipe --no-tests=error` (label `lavapipe`, 4 jobs): libSceAgcDriver suites, Recorder and HostImport. Windows because the driver loads `vulkan-1.dll` and builds only with MinGW. Fixtures `GTEST_SKIP` when no device exists; ctest counts that as a pass, so the test step runs with `-V` and fails the job if any suite logs `no Vulkan device` or `VK_EXT_external_memory_host unavailable` (the HostImport skip), and a pre-test step fails if the loader or the x64 ICD manifest is missing. No GPU, no game data. |
| unit | Part of `build_and_test`: `ctest` over libc, libkernel and relinker tests, including the futex sync tests (Milestone 1). Runs with `CTEST_PARALLEL_LEVEL=4`: fixtures use timestamp-unique dirs and timing asserts are lower-bounds, so parallel execution is load-safe. |
| recompiler-golden | Part of `build_and_test` (merged, was a separate runner): replays serialised shader requests through the recompiler: `ctest --preset golden` (coverage gate plus wave32/64 replay of every decoded class) and `agc_shader_replay --golden core/shader/recompiler/tests/golden/corpus` (diffs disassembled SPIR-V against checked-in `.spvasm` goldens). Each module is validated with SPIRV-Tools `spirv-val` pre- and post-optimizer inside `Recompile`. The hosted corpus holds only **synthetic or hand-assembled RDNA2 shaders** written for the project, with no game bytecode. Game-derived shader requests are captured into a local-only corpus on the maintainer machine and replayed by local regression (§2, step 4) with the same tool. |
| policy | Step of `build_and_test`. Fails on new title-specific code patterns and stray switches: no `APS5_` string literals and no `getenv` outside the `Config` module (which alone may name the `APS5_` prefix, to warn about stale variables); test sources under `*/tests/` are exempt from the `getenv` check because they verify environment behavior without changing runtime behavior ([configuration.md](configuration.md)); no title-ID literals in `core/` except `*/tests/`, which need realistic IDs as fixture data; no hash-matched kernel tables; the `PORTPS5_WORKAROUND` registry matches `docs/workarounds.md` in both directions with mechanism-only key names. Compliance with the rule that docs describe hardware only as the generic reference tier is enforced via PR review checklist. |
| **nightly** | Scheduled (03:17 UTC) and manually dispatchable. Builds only the targets behind `slow`-labelled tests and runs `ctest --preset slow`: full-size fuzz runs too long for the hosted `unit` job. Today: the uncapped 10^6-operation extent-allocator differential (`GuestArenaExtent.FuzzUncapped`, ~3 min). The hosted `unit` job runs a capped variant of the same test, so PRs still cover the invariant. Slow tests are `DISABLED_` in the gtest binary and registered with `portps5_add_test` under label `slow`. |
| **codeql** | GitHub CodeQL SAST (C/C++, `security-extended` query suite). Performs semantic dataflow analysis of the project's own source (`core/`, `tools/`) for buffer overflows, integer overflows, use-after-free, and format-string bugs. Third-party submodules are excluded from the build (`ANYPS5_ENABLE_SPIRV_TOOLS=OFF`) so CodeQL's compiler-interception database never contains third-party code. Results appear in the GitHub Security → Code scanning tab as inline SARIF alerts on PRs. No external service account or token required; uses the automatic `GITHUB_TOKEN`. Also runs on a weekly schedule so new query packs surface vulnerabilities even with no code changes. |
| **gitleaks** | Secret and legal-boundary scan of every push and PR diff. Detects private keys, API tokens, and Sony-specific patterns (IDPS keys, `.rap`/`.rif` licence content, firmware paths, AES-128 key material) as defined in `.github/gitleaks.toml`. Extends Gitleaks's built-in provider ruleset. No external token required. |
| **doxygen-doc-gate** | Runs Doxygen (`docs/Doxyfile`) on `core/libs/prx` and `core/relinker` on every PR and push to `main` as a required status check. Configured with `WARN_AS_ERROR = FAIL_ON_WARNINGS` and `WARN_IF_DOC_ERROR = YES`. Fails if any doc comment contains malformed markup, scanning the entire source tree to report all violations before failing (`WARN_IF_UNDOCUMENTED` and `WARN_NO_PARAMDOC` are disabled initially to avoid blocking on inherited pre-existing debt and will be re-enabled incrementally). On failure, `build/doxygen_warnings.log` is uploaded as the `doxygen-warnings` artifact. This job does not require the MinGW toolchain and runs on a plain `windows-2022` runner with the official Doxygen 1.18.0 zip installed from doxygen.nl (SHA-256 pinned in the workflow). Complements the in-tree Python policy checker (`tools/check_comments.py`), which enforces PortPS5-specific rules (file-level headers, `APS5_VABI` doc coverage, `TEST()` invariant comments) on every changed file. |
| **progress-report** | Runs `tools/progress.py` on every PR (no `main` filter): renders the base and head implementation counts (declared `APS5_VABI` functions in `core/libs/prx`, RDNA opcodes vs `tools/rdna_isa.txt`) and posts the delta as a PR comment (`progress-comment.yml`). Static source scan, no GPU, no game data. Full-site render plus badges deploy from `main` pushes via `workflows/progress.yml` (needs GitHub Pages enabled). |
| **python-quality** | Astral toolchain gate for every Python file (`tools/`, `tests/tools/`, relinker self-tests): `ruff check` + `ruff format --check` and `pytest` with coverage over `tools/` (`fail_under = 85`, `pyproject.toml`). Runs on `ubuntu-latest` via pinned `uv` (`uv.lock` committed); versions pinned in `pyproject.toml` (`dependency-groups.dev`). CTest keeps running the same suites through stdlib `unittest` on Windows so hosted unit execution never depends on PyPI. |

- **Status as of 2026-10-01:** every job above exists in `.github/workflows/`. `driver-lavapipe` (bean `portps5-ekx3`) first ran green on `main` with PRs #80/#82 (`0bb6edf1`). `main` CI was green through #53 (`07b76f75`) and is red since: `build_and_test` fails `prx_cross_import_check` on the #68 and #88 merges (`eaea7340`, `1c65844a`; bean `portps5-sjuv`), and the #88 run also failed `driver_lavapipe` on a segfault in `RecorderTest.SubmitMakesDeviceWritesVisibleToTheHost` (bean `portps5-3maf`). `tools/regress.py` (conversion layout, launch, results JSON writer, pass rule) landed in PR #76 (bean `portps5-3m3u`); the frame-check, checkpoint-replay, shader-corpus and save steps of section 2 and the upload are still open. The runtime telemetry core (section 4.3) writes the section 4.1 log, but nothing calls its `Start` at process start-up yet, so `run` reports a missing log until that wiring lands (bean `portps5-w1re`).
- [x] `driver-lavapipe` passes on `main` (first green run with PRs #80/#82, `0bb6edf1`).
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

- **Pass rule:** `debug_keys_set` is empty, `crashes` = 0, `softlocks` = 0, every `checkpoints` and `fmv` entry is `pass`, `save_roundtrip` is not `fail`, and in warm-cache runs `spirv_compilations` = 0 and `pipeline_creations_after_warmup` = 0 (`tools/regress_metrics.py` `fail_reasons`). A run that fails any of these reports `result: "fail"`. Full runs add the bar in §4.2.
- **Derived fields:** `audio_underruns_per_10min` = `n * 600 / max(duration_s, 60)` over the summed `audio.underrun` counts. `fps.min` = 1000 / the slowest non-stall frame time.
- **Check inputs:** `checkpoints`, `fmv` and `save_roundtrip` come only from `--checks-file`, which no step writes yet. Until the frame-check and save steps exist, `save_roundtrip` defaults to `not_run`, so every `full_run` fails on it (bean `portps5-3m3u`).
- The results JSON carries no personally identifying hardware detail beyond GPU vendor, driver and tier.

### 4.1 Telemetry log contract (`portps5.telemetry/1`)

The runtime writes `<install>/logs/telemetry.jsonl`, one JSON object per line, always on and independent of any `[debug]` key. `tools/regress.py` reads only the whitelisted fields below and ignores unknown events and keys, so no game text can reach the results. The first record must be `run.start`; malformed lines are errors, never skipped, and blank lines are ignored.

| `ev` | Fields | Used for |
|---|---|---|
| `run.start` | `schema`, `resolution` (`WxH`), `pipeline_cache` (`warm`/`cold`), `audio_device` | header fields |
| `frame` | `dt_ms` (time since the previous present), `t_ms` (monotonic ms since `run.start`, optional for older runtimes) | `fps`, stalls, softlock gaps, tail hang check |
| `heartbeat` | `t_ms` (runtime clock, written about once per second by the watchdog thread) | tail hang check |
| `softlock` | `idle_ms`, `reason` (1 present, 2 guest thread; not consumed) | watchdog report (no present, or no guest thread progress, over 30 s) |
| `crash` | none | `crashes`. No producer yet; crashes are inferred from a missing `run.end` or a non-zero exit (§4.2). |
| `audio.underrun` | `n` (default 1) | `audio_underruns_per_10min` |
| `av.offset` | `ms` | `av_offset_ms_max` (absolute maximum) |
| `video_latency_ms` | `ms` | informational, not consumed |
| `warmup.end` | `warmup_ms` | `warmup_ms`; later `pipeline.create` events count. Producer not wired (bean `portps5-w1re`). |
| `spirv.compile`, `pipeline.create` | none | `spirv_compilations`, `pipeline_creations_after_warmup`. Producer not wired (bean `portps5-w1re`). |
| `run.end` | `capture_split`, `write_faults` | clean end marker and totals |

`run.start` and `run.end` are written by `Start` and `Shutdown`, which no real run calls yet (§4.3), so `capture_split` and `write_faults` stay 0 in real results until that wiring lands.

### 4.2 Runner rules (`tools/regress.py`)

- **Statistics:** stalls (`dt_ms` > 1000) are excluded from `fps` and counted in `fps.stalls`. `p1_low` = 1000 / mean of the slowest 1% (rounded up, at least one frame) of the remaining frame times. `duration_s` is the span of presented frames, the quantity the 30 minute rule uses.
- **Softlocks:** `softlocks` is the larger of the watchdog's `softlock` events and the count of present gaps over 30 s, so a silent watchdog cannot hide one. For a run the runner ended at its time limit, more than 30 s between the last present and the last `heartbeat`, both on the runtime's clock (never the runner's wall clock), is one more softlock, because no later `frame` record carries that gap. A title that never presented is measured from `t_ms` 0, with no loading exemption. The check is skipped without heartbeats (older runtime) and when frames carry no `t_ms`, since there is then no common clock.
- **Crashes:** `crash` events, plus one if the title ended by itself without `run.end` or with a non-zero exit code. A run the runner ends at its time limit is not a crash. `run` persists `{killed, exit_code, wall_ms}` to `logs/runner.json`, and `report` reuses it, so re-reporting a timed run gives the same verdict.
- **Config:** `config_sha256` hashes the canonical JSON of `<install>/config/global.toml` overlaid by `games/<titleId>.toml`; `workarounds_set` lists enabled `[workarounds]` keys and `debug_keys_set` every `[debug]` key present. The runner refuses to run with `PORTPS5_DEBUG` set, so the hash reproduces from files. Enabling `[debug] profile` for a run therefore makes it `fail`, by the pass rule above. Known gap: `prepare` copies no `config/` into the install dir (its docstring says it does), so `config_sha256` hashes `{}` until that is fixed.
- **Additive fields:** `fail_reasons` (names of failed rule fields only), `fmv` (per-FMV `pass`/`fail` from the "FMV played" rule in [video-fmv.md](video-fmv.md)). `save_roundtrip` may be `not_run`, which fails a `full_run`. `checkpoints`, `fmv` entries and `save_roundtrip` come from `--checks-file`, written by the frame-check and save steps.
- **Full-run pass:** all rules above plus average ≥ 30 fps, 1% low ≥ 20 fps, `duration_s` ≥ 1800, resolution ≥ 1920×1080, warm cache and `save_roundtrip` = `pass`.
- **Layout:** `prepare` runs `relinker --windows` (never `--skip-sce-module`, which crashes guest libc++ code), copies the runtime libs and links `app0` to the dump (read-only). `run` launches `game.exe` with the install dir as cwd, sends stdout and stderr to files it never reads, and refuses any path inside the repository.

**Run locally** (Windows, `release` preset, `C:\mingw64\bin` first on PATH; paths are placeholders, quote them):
`python tools/regress.py prepare --dump "<dump dir>" --install "<out dir>" --relinker build\release\core\relinker\relinker.exe --libs build\release\core\libs\libs`, then `python tools/regress.py run --install "<out dir>" --duration-s 2100 --run-type full_run --title-id <id> --region <r> --patch <p> --name "<n>" --commit <sha> --gpu-vendor <v> --driver-version <d> --bench-cpu <n> --bench-gpu <n> --checks-file "<checks.json>"`. Review the written `compat/results/<titleId>/<commit>-<run_type>.json` (metrics, hashes, pass/fail only) before opening a PR. Raw logs, stdout and saves stay in `<out dir>`.

### 4.3 Producer: runtime telemetry (`core/libs/prx/libc`)

- **Core:** `include/telemetry/Telemetry.hpp` is pure and clock-injected (`Log`, `Watchdog`, `Sampler`); `src/Telemetry.cpp` adds the file sink (`<install>/logs/telemetry.jsonl`, flushed per record), one 1 s polling thread and the process-wide instance. Telemetry is always on and uses no `[debug]` key or environment variable.
- **Interface** (`include/telemetry/TelemetryRuntime.hpp`, verbatim `_nid_no_patch` C exports, no-ops before `Start`): `Start`, `NotePresent` (frame record plus watchdog heartbeat), `NoteGuestProgress`, `Event` (numeric-only: `spirv.compile`, `pipeline.create`, `warmup.end`; `dialog.open` is planned: the dialog modules log text only and do not emit it), `SetAudioSource`, `SetVideoLatencyMs`, `SetFmvWindow`, `SetDiagnosticsHook`, `Shutdown` (writes `run.end` and releases the log file handle).
- **Watchdog:** once per second it writes a `heartbeat` record (runtime clock) and checks both heartbeats. No present for more than 30 s, or, once `NoteGuestProgress` has been called at least once, no guest progress for more than 30 s, writes a `softlock` event (`idle_ms`, `reason` 1 = present, 2 = guest thread), runs the diagnostics hook (per-queue state), writes `run.end` and aborts through `Unsupported`. It never skips work. Each half stays off until its heartbeat is first seen (`NotePresent`, `NoteGuestProgress`), so boot, a long first load or an unwired caller cannot false-positive; a title that never presents is caught by `tools/regress.py` (silence from run start to the kill). `run.end` is final: the `Log` drops every record after it, and exports are no-ops after `Shutdown`.
- **Audio and A/V:** the single mixer (PR #45) registers its `GetUnderruns` and `GetLatencyMs` as the audio source (process-wide, so a registration made before `Start` is kept; the same holds for `SetDiagnosticsHook`); the sampler emits `audio.underrun` with per-second deltas, and while an FMV window is open `video_latency_ms` and `av.offset` = audio latency minus video latency ([video-fmv.md](video-fmv.md)).
- **Tests:** `telemetry_core_tests` (GoogleTest, synthetic clock, no sleeps): record schema, first-present baseline, 30 s boundary, latch, present check gated on the first present, `run.end` finality, synthetic stalled thread, underrun deltas, A/V offset. `telemetry_runtime_tests`: Start/Shutdown lifecycle on disk, nothing after `run.end`, an audio source registered before `Start` is sampled.
- **Open wiring (bean `portps5-w1re`):** the `Start` call at process start-up (needs `Config::Loader::Initialize` at startup, bean `portps5-c06p`), the presenter's `NotePresent` and `SetVideoLatencyMs` calls (AGC driver work in flight), and a guest-thread `NoteGuestProgress` site. A watchdog abort ends with `softlock` plus `run.end`, which `tools/regress.py` counts as one softlock and no crash.

### 4.4 Performance fields and baseline comparison (planned)

Target design for the performance track ([ROADMAP.md](../ROADMAP.md) "Performance track", PRD §4.5). Nothing here is implemented yet. All fields are additive to `portps5.results/1`, and an older runtime that does not write them produces results without them.

- [ ] **Frame-time percentiles** (bean `portps5-rrll`): `frame_ms: { p50, p90, p99 }` over the same non-stall frames as `fps`.
- [ ] **Baseline compare** (bean `portps5-rrll`): `tools/regress.py compare --baseline <json> --result <json>` prints per-field deltas and exits non-zero on a regression. Both results must match on title pin, `run_type`, `host_tier`, `resolution`, `pipeline_cache` and `config_sha256`, or the compare refuses. Proposed thresholds, to be tuned with run-to-run noise data: `fps.avg` down more than 3 %, `fps.p1_low` down more than 5 %, or any violation counter up. The compare reads only results JSON and never uploads.
- [ ] **Frame breakdown** (bean `portps5-hfiw`): optional numeric fields on the `frame` record, `cpu_ms` (guest frame CPU time until submit), `rec_ms` (driver record and submit), `gpu_ms` (Vulkan timestamp queries around the frame's submits, converted with `timestampPeriod`), `wait_ms` (CPU blocked on a GPU fence) and `present_ms` (blocked in present). The results JSON gets `frame_breakdown_ms: { cpu, rec, gpu, wait, present }` as per-frame means. A device without timestamp support on the queue omits `gpu`. The fields are numbers only, so no game data reaches the log.
- [ ] **Violation counters** (bean `portps5-aifo`): a numeric-only `perf.violation` event with `kind` (1 = P1 CPU wait or readback not requested by the guest, 2 = P2 Vulkan object creation per draw/dispatch, 3 = P3 compile on the submit thread, 5 = P5 full compare or copy of guest memory proven unchanged) and `n`. These are counted only after `warmup.end`. The results JSON gets `invariants: { p1, p2, p3, p5 }`. They are reported, not part of the 1.0 pass rule (PRD §4.5).
- [ ] **Run metadata** (bean `portps5-g2ll`): `run.start` adds a build preset id, the short commit baked at build time, the Vulkan vendor id, driver version, present mode and resolution scale. `tools/regress.py` prefers these over its command-line flags and warns when they disagree, so the compare's matching rule uses the runtime's own values.
- [ ] **Crash producer** (bean `portps5-yhj7`): a top-level unhandled-exception filter writes `crash { code }` and `run.end` before the process ends, so `crashes` no longer depends on exit-code inference.
- [ ] **Memory** (bean `portps5-ybq3`): a per-second numeric `mem` event (VRAM used and budget from `VK_EXT_memory_budget`, guest committed bytes, host-import bytes, staging bytes); the results JSON gets `memory_peak_mb`.
- [ ] **Stall attribution** (bean `portps5-26s5`): a `frame` record over the stall threshold carries a numeric `cause` (compile, load or I/O, GPU wait, guest), from the breakdown and the violation counters; the results JSON gets `stalls_by_cause`.
- [ ] **Boot and compile timing** (beans `portps5-pk9m`, `portps5-tg51`): `boot_ms` (process start to first present), and `shader.compile_ms` and `pipeline.create_ms` events with totals and p99 in the results JSON.
- [ ] **Dialogs** (bean `portps5-7qon`): each dialog `Open` emits the numeric `dialog.open { kind }` event, and no dialog logs guest text outside `[debug]` tracing.

## 5. Test Framework Architecture (GoogleTest & GMock)

PortPS5 standardizes on **GoogleTest (GTest) and GMock** for all unit, integration, and subsystem tests, replacing ad-hoc standalone `main()` executables.

- **Unified Test Suites:** Instead of separate executables for every individual test function, tests are grouped into cohesive suite binaries (`kernel_tests`, `libc_tests`, `relinker_tests`, `shader_recompiler_tests`, `audio_tests`) linked against `GTest::gtest` and `GTest::gmock`.
- **Expression Decomposition:** All test assertions use GoogleTest macros (`EXPECT_EQ`, `EXPECT_NE`, `ASSERT_TRUE`, `EXPECT_THAT`) so that failures report file, line, expression, and actual vs expected values (including formatted SCE/errno error codes).
- **Death Testing (`EXPECT_DEATH`):** Fatal errors in `core/` call the logging abort path (`APS5_ABORT`). GoogleTest death tests verify that invalid guest pointers, misaligned addresses, or unsupported parameters abort cleanly with the expected log message without crashing the test runner process.
- **Hardware/System Mocking (`GMock`):** Subsystems depending on external hardware (Vulkan physical devices, SDL2 audio streams, DualSense gamepad endpoints) use GMock classes to simulate device state, timing, and error conditions deterministically.
- **Reference Test Porting & Ecosystem Ingestion:** Over 55,000 lines of subsystem tests from KytyPS5, sandbox security/threading tests from SharpEMU, FreeBSD 12 kernel/libc tests (`kqueue`, `umtx`, `mmap`), Mesa ACO RDNA2 instruction vectors, Wine NTDLL synchronization/memory tests, and shadPS4/RPCS3 container and ATRAC9 media suites are systematically adapted into PortPS5 GoogleTest suites. All Title IDs are scrubbed to generic synthetic constants (e.g. `PPSA00000`) per legal and policy rules ([TESTING.md](../TESTING.md)).
