# PortPS5 — Spec: Configuration

Status: draft v1 · 2026-09-27

## Scope

This spec covers runtime configuration (file locations, layering, the TOML schema, validation and the `PORTPS5_DEBUG` variable), the rules for per-title workarounds (`docs/workarounds.md`), and removing PR #5's `APS5_*` environment switches.

Offline NP, PSN and trophy behaviour is **not** configurable, and is specified in [save-data.md](save-data.md).

PRD bar owned here: F6 (a per-game TOML keyed by title ID that holds resolution scale, present mode and documented workarounds).

## Current state

- **`main` (`e06dbff`):** no `APS5_*` reads and no config file.
- **PR #5 (`29b4601`):** 307 unique quoted `APS5_*` names (`git grep -ohE '"APS5_[A-Z0-9_]+"' | sort -u`) and 420 `getenv(` calls under `core/`.
  - The usual pattern is a function-local `static const bool x = std::getenv("APS5_...") != nullptr;`. Each switch is read once, and **any value, including `0`, turns it on**. Examples: `libSceAgcDriver/Execution/src/Driver.cpp:1624`, `libSceAgcDriver/Graphics/src/State.cpp:78`.
  - Numeric switches parse with `atoi`/`strtod` and fall back silently. For example, `APS5_GPU_WAIT_TIMEOUT_MS` defaults to 1000 (`Driver.cpp:4743`), and `APS5_TIME_SCALE` ignores non-positive values (`libkernel/Time/Time.cpp:36-46`).
  - 261 of the (module, name) pairs are in `libSceAgcDriver`. The rest are spread over the recompiler, libkernel, libc, VideoOut, AudioOut, Ajm, Fiber and Agc.
- **Present mode** is hard-coded to FIFO (`libSceAgcDriver/Execution/src/VulkanDevice.cpp:847`, `:1471`). There is no resolution-scale control.
- **Title ID** comes from `/app0/sce_sys/param.json` (`libkernel/AppMetadata/src/AppMetadata.cpp:22-37`), so the runtime can select a per-title file.
- **No TOML parser** is in the tree. The submodules are SDL2, Vulkan-Headers, SPIRV-Headers, SPIRV-Tools, glslang, VMA and LibAtrac9.
- **PortPS5 `feat/m1-runtime-core` (this branch):** `core/libs/prx/libc/src/Config.cpp` (`include/config/Config.hpp`) parses and validates `config/global.toml`, `config/games/<titleId>.toml` and `PORTPS5_DEBUG`, reporting `file:line: key: reason`; toml++ v3.4.0 is vendored at `3rdparty/tomlplusplus/toml.hpp` (pinned in [build-toolchain.md](build-toolchain.md)). Still open on this branch: call `Loader::Initialize` at startup with the `param.json` title ID, copy `config/` at conversion time, wire `display.present_mode` / `resolution_scale` in the driver, and emit the results-JSON fields.

The 307 PR #5 switches fall into these classes (by name pattern, then reviewed):

| Class | Count | Examples | Disposition |
|---|---|---|---|
| Kill switch (`APS5_NO_*`) | 121 | `NO_FILL_HLE`, `NO_ADJACENT_GENERATION`, `NO_WRITE_WATCH`, `NO_PAD_INPUT` | Deleted, together with the path it disables, once its verify mode passes. |
| Alternative behaviour or ordering | 67 | `LABEL_*` (9), `CPU_COPIES`, `FULL_BARRIERS`, `IGNORE_DEPTH_TEST`, `SAVEEXEC_WRITE_FIRST`, `TIME_SCALE`, `SPIRV_OPT` | Deleted. One code path is kept. |
| Tuning (sizes, caps, affinity, timeouts) | 35 | `HOST_IMPORT_MIB`, `*_CACHE_ENTRIES`, `JOB_AFFINITY_MASK`, `GPU_WAIT_TIMEOUT_MS`, `FLIP_INFLIGHT` | Becomes a constant or is sized automatically ([gpu-driver.md](gpu-driver.md), [guest-memory.md](guest-memory.md)). Affinity switches are removed: affinity is recorded, not applied ([threading.md](threading.md)). Only `HOST_IMPORT_MIB` and `HEAP_CACHE_MIB` survive, as `[debug]` overrides of the automatic size. |
| Forced serialisation (`APS5_SYNC_*`) | 6 | `SYNC_DRAWS`, `SYNC_FLIP`, `SYNC_DISPATCH` | Deleted. See the open questions. |
| Trace (`APS5_TRACE_*`) | 39 | `TRACE_AUDIOOUT2`, `TRACE_AJM`, `TRACE_LABEL` | `[debug] trace`. |
| Dump (`APS5_DUMP_*`) | 11 | `DUMP_SHADERS`, `DUMP_IR`, `DUMP_FRAMES` | `[debug] dump`. |
| Profile | 2 | `PROFILE_DRAW`, `PROFILE_GPU` | `[debug] profile`. |
| Validate, verify or watch | 26 | `VERIFY_RECIPE`, `BARRIER_VALIDATE`, `WATCH_ADDR`, `SRT_DEBUG` | `[debug] validate`, `[debug] watch`. |

That leaves **78** diagnostic switches and **2** size overrides, which survive as typed `[debug]` values. The other **227** are removed.

## Decision

This follows the decision table in [README.md](README.md#subsystem-specs) §Configuration and §Global policy. The configuration files are `config/global.toml` and `config/games/<titleId>.toml`, with the sections `[display]`, `[input]`, `[workarounds]` and `[debug]`. A typed schema is validated at start-up, and **an unknown key is an error**. Every `[workarounds]` key is documented in `docs/workarounds.md`. `APS5_*` switches are removed, and kill switches go once their verify mode has passed. Diagnostics and overrides survive only as `[debug]` values or one `PORTPS5_DEBUG` variable, and never as behaviour-changing toggles.

## Target design

**Locations and layering.** The runtime reads `<install>/config/global.toml`, then `<install>/config/games/<titleId>.toml`, then `PORTPS5_DEBUG`. Later layers override earlier ones key by key. `<install>` is the directory of the converted executable, and the CLI copies the repository's `config/` there at conversion time. `global.toml` may not contain `[workarounds]`. `PORTPS5_DEBUG` may set only `[debug]` keys, with the syntax `key=value;key=v1,v2` (for example `trace=audio,ajm;dump=shaders`). Booleans there read `true`/`false` (also `1`/`0`); `watch` cannot be set through `PORTPS5_DEBUG` because its table shape needs a TOML file.

**Required top-level keys.** `schema = 1`. In game files, also `title_id = "PPSA01342"`, which must match both the file name and `param.json`.

**Schema.** This table is the complete schema; any key not listed here is a start-up error.

**`[debug]` rule.** `[debug]` keys are diagnostic or override only. They are never needed for a passing run, a release run must not set any, and the results JSON records every `[debug]` key that is set (`debug_keys_set`, [verification.md](verification.md) §4).

| Section.key | Type | Default | Notes |
|---|---|---|---|
| `display.resolution_scale` | float 1.0–2.0 | 1.0 | Internal render-target scale ([gpu-driver.md](gpu-driver.md)). Not part of the 1.0 performance bar: the gate is measured at scale 1.0. |
| `display.present_mode` | enum `fifo`/`mailbox`/`immediate` | `fifo` | Falls back to `fifo` with a warning if the surface lacks it. |
| `display.fullscreen` | bool | false | F11 still toggles. |
| `display.window_percent` | int 25–100 | 60 | Initial window size as a share of the display. PR #5 commit `9453f47` hard-codes 60%. |
| `input.*` | see [input.md](input.md) | | `deadzone`, `mouse_look`, `mouse_sensitivity`, `swap_confirm`, `bindings`. |
| `workarounds.<key>` | typed per key | off | Allowed only in game files. The key must be registered in code **and** documented. |
| `debug.log_level` | enum `error`/`warn`/`info`/`debug` | `info` | |
| `debug.trace` | array of enum | `[]` | Categories: `audio`, `ajm`, `pad`, `savedata`, `dialog`, `np`, `label`, `sync`, `timers`, `memory`, `bda`, `bindless`, `fiber`, `exit`, … One category per former `TRACE_*` family. |
| `debug.dump` | array of enum | `[]` | `shaders`, `targets`, `textures`, `queue`, `rejected`. IR and frame dumps use `debug.recompiler.dump_ir` and `debug.gpu.dump_frames`. |
| `debug.dump_dir` | path | `<install>/dumps` | Must be outside the saves directory. |
| `debug.profile` | array of enum | `[]` | `gpu`. Per-draw shader profiling uses `debug.recompiler.profile`. |
| `debug.validate` | array of enum | `[]` | `recipes`, `barriers`, `copies`, `shadows`, `indirect_args`. Validation only logs mismatches. It never changes what the runtime produces. |
| `debug.watch` | array of `{addr: u64, write: bool}` | `[]` | Replaces `WATCH_ADDR` and `WATCH_WRITE`. |
| `debug.ignore_host_input` | bool | false | For recorded-input replay. Replaces `NO_PAD_INPUT`. It filters host input and does not change the guest path. |
| `debug.gpu.host_import_mib` | int | auto | Override of the host-import budget ([gpu-driver.md](gpu-driver.md)). Replaces `HOST_IMPORT_MIB`. |
| `debug.gpu.trace` | bool | false | GPU command tracing ([gpu-driver.md](gpu-driver.md)). |
| `debug.gpu.dump_frames` | bool | false | Frame dumps ([gpu-driver.md](gpu-driver.md)). Replaces `DUMP_FRAMES`. |
| `debug.memory.heap_cache_mib` | int | auto | Override of the large-block heap cache cap ([guest-memory.md](guest-memory.md)). Replaces `HEAP_CACHE_MIB`. |
| `debug.recompiler.dump_ir` | array of string | `[]` | Code addresses to dump IR for, or `"all"` ([shader-recompiler.md](shader-recompiler.md)). Replaces `DUMP_IR`. |
| `debug.recompiler.single_lane` | bool | false | Single-lane execution for bisecting ([shader-recompiler.md](shader-recompiler.md)). Replaces `SINGLE_LANE`. |
| `debug.recompiler.profile` | bool | false | Per-shader profiling ([shader-recompiler.md](shader-recompiler.md)). Replaces `PROFILE_DRAW`. |
| `debug.recompiler.capture` | bool | false | Captures `.req` files into the local-only corpus ([shader-recompiler.md](shader-recompiler.md)). |
| `debug.pipeline_cache` | enum `on`/`off`/`readonly` | `on` | Pipeline cache mode ([pipeline-cache.md](pipeline-cache.md)). |
| `debug.pipeline_cache_verify` | bool | false | Recompiles on a cache hit and compares the SPIR-V ([pipeline-cache.md](pipeline-cache.md)). |
| `debug.relinker.trace_sse4a` | bool | false | Traces the runtime SSE4a trap ([relinker.md](relinker.md)). |
| `debug.threading.dump_futex_owners` | bool | false | Log owner thread IDs of contended futex words on watchdog softlock ([threading.md](threading.md)). |

**Validation.** The TOML parser is toml++ (MIT, header-only; a proposal, to be pinned in [build-toolchain.md](build-toolchain.md)).

Each of the following is a start-up error, reported as `file:line: key: reason` with a non-zero exit before any guest code runs: an unknown section or key; a wrong type, or an out-of-range or unknown enum value; a missing `schema`, or one newer than the runtime supports; a `title_id` mismatch; `[workarounds]` in the global file; a workaround key that is not registered.

Any `APS5_*` variable in the environment triggers one warning that lists the names and says they are ignored. It is not an error, because stale shells are common.

**Code API.** A single `Config` module parses once and exposes typed getters, for example `Config::Display().presentMode` and `Debug::Traces(Trace::Audio)`. Workarounds are declared with `PORTPS5_WORKAROUND(key, type, default, "mechanism")`. The macro registers the key with the schema and names the mechanism it toggles. The `policy` CI job enforces this: no `APS5_` string literals and no `getenv` outside the `Config` module (which alone may name the `APS5_` prefix, to warn about stale variables); test sources under `*/tests/` are exempt from the `getenv` check.

**`docs/workarounds.md` rules.** Each entry lists the key; its type and default (the default is always the general behaviour); the **mechanism** it toggles, in engine-neutral words; why no general fix exists yet; the title IDs and patch pins that set it; the tracking issue; and the removal condition. Key names describe mechanisms, for example `copy_kernel_linear_match`, and never games. The `policy` job fails when a key name contains a title name or a title-ID pattern, when a registered key and the docs disagree in either direction, or when a game file sets an undocumented key. A key that no gate title uses at a milestone exit is deleted.

**Migration from `APS5_*`.** The fork never contains `getenv("APS5_")`. Conversion happens as each PR #5 piece is ported in M1:

| Step | Action |
|---|---|
| 1 | Land `Config` and `[debug]` first in M1. Wire the policy check (verification.md §1). |
| 2 | Port a PR #5 file. Map every trace, dump, profile and validate read to `Debug::*` (78 names → about 25 enum values). |
| 3 | For each kill switch or alternative-behaviour switch in the file, keep the default path and delete the other one. If the file's M1 verify run fails without the alternative path, the alternative becomes a candidate `[workarounds]` key, subject to the rules above. |
| 4 | Replace tuning reads with constants, and add a `TODO(auto-size)` pointing to the spec that owns the size. |
| 5 | Record every name in a table `APS5 name → disposition` in the M1 seed, as the audit trail. |

## Interfaces

| Peer spec | Keys or contract |
|---|---|
| [gpu-driver.md](gpu-driver.md) | `display.resolution_scale`, `display.present_mode`, `debug.gpu.*`. Auto-sizing of the former tuning switches (host import, caches, flip in-flight). |
| [guest-memory.md](guest-memory.md) | `debug.memory.heap_cache_mib`. The `memory` trace category. |
| [input.md](input.md) | The `[input]` section. |
| [audio.md](audio.md), [video-fmv.md](video-fmv.md), [save-data.md](save-data.md) | Only `debug.trace` categories. They have no behavioural keys. Offline NP, PSN and trophy behaviour is in [save-data.md](save-data.md). |
| [shader-recompiler.md](shader-recompiler.md), [pipeline-cache.md](pipeline-cache.md) | `debug.dump` = `shaders`, `debug.recompiler.*`, `debug.pipeline_cache`, `debug.pipeline_cache_verify`. Config values never enter pipeline-cache keys, except `resolution_scale` if it changes pipelines. |
| [threading.md](threading.md) | Affinity is recorded, not applied, and not configured. Only `debug.trace` categories (`sync`, `timers`). |
| [relinker.md](relinker.md) | The CLI copies `config/` to `<install>` at conversion time. `debug.relinker.trace_sse4a`. |
| [build-toolchain.md](build-toolchain.md) | toml++ pin. |
| [verification.md](verification.md) | The `policy` job rules. Results JSON records `config_sha256` (the resolved config hash), `workarounds_set` and `debug_keys_set`. |

## Failure modes

| Failure | Handling |
|---|---|
| A config error of any kind | Start-up error with the location. There is no partial start. |
| Missing game file | Globals apply. This is normal. |
| Missing global file | Built-in defaults apply, and one warning is logged. |
| A present mode the surface does not support | Fall back to `fifo`, with a warning. |
| A debug dump would fill the disk | Dumps stop at a 2 GiB cap per run, and a warning is logged. |
| A stale `APS5_*` environment variable | Warning. The variable is ignored. |

## Tests

- **Unit (hosted):**
  - Parse a golden file for every key.
  - One reject case per validation rule: unknown key, bad type, out of range, workarounds in global, `title_id` mismatch, schema too new.
  - Layering order.
  - `PORTPS5_DEBUG` parse and rejection of non-debug keys.
- **`policy` job:** no `APS5_` string literals and no `getenv` outside the `Config` module (which alone may name the `APS5_` prefix, to warn about stale variables); the workaround registry matches `docs/workarounds.md` in both directions; no title names or title-ID patterns in key names.
- **Local regression:** each gate title runs with its shipped game file. The results JSON lists the workarounds and `[debug]` keys that were set; a pass requires no `[debug]` keys.

## Milestones

| Milestone | Work |
|---|---|
| M1 | `Config`, schema, validation and `[debug]` landed with unit tests and `policy` enforcement (done). Still open: startup call with the `param.json` title ID, `config/` copy at conversion, `display` key wiring, results-JSON fields. Until then, 0 `APS5_*` reads and per-game TOML (F6) are not closed. |
| M2 | `[input]` bindings (with [input.md](input.md)). The regression tooling records the config hash. |
| M3 | Any `[workarounds]` keys Tomb Raider needs, each with a docs entry. |
| M4 | Exit criterion: every `[workarounds]` key used by a gate title has a `docs/workarounds.md` entry. |
| M5 | Demon's Souls passes with title-specific behaviour only in its documented TOML. |
| M6 | The user guide documents every key. `docs/workarounds.md` is published. |

## Open questions

- Keep one `[debug] serialize_gpu` switch for bisecting ordering bugs? It changes timing, not results, but it conflicts with the rule against behaviour-changing toggles, so it is excluded for now.
- A user-override directory, for example `%APPDATA%/PortPS5/config`, for players who reinstall. It is not needed for 1.0.
- Does `resolution_scale` belong in pipeline-cache keys? Decide with [pipeline-cache.md](pipeline-cache.md).
- Enforce `debug.dump_dir` outside the saves directory once [save-data.md](save-data.md) owns the saves path (M2). Until then the check is non-empty only.
