<p align="center">
  <img src="assets/logo.png" alt="PortPS5 Logo" width="180" />
</p>

# PortPS5

[![CI](https://img.shields.io/github/actions/workflow/status/yuriolive/PortPS5/ci.yml?branch=main&label=CI&style=flat-square)](https://github.com/yuriolive/PortPS5/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/yuriolive/PortPS5/codeql.yml?branch=main&label=CodeQL&style=flat-square)](https://github.com/yuriolive/PortPS5/actions/workflows/codeql.yml)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/yuriolive/PortPS5?style=flat-square&utm_source=oss&utm_medium=github&utm_campaign=yuriolive%2FPortPS5&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0--only-blue?style=flat-square)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows%2010%2F11-informational?style=flat-square&logo=windows)](https://github.com/yuriolive/PortPS5)
[![Language: C++23](https://img.shields.io/badge/language-C%2B%2B23-informational?style=flat-square&logo=cplusplus)](https://github.com/yuriolive/PortPS5)
[![Vulkan 1.3](https://img.shields.io/badge/Vulkan-1.3-red?style=flat-square&logo=vulkan)](https://www.vulkan.org/)
[![GitHub Sponsors](https://img.shields.io/github/sponsors/yuriolive?style=flat-square&logo=githubsponsors&color=ea4aaa)](https://github.com/sponsors/yuriolive)
[![libraries](https://yuriolive.github.io/PortPS5/badge-libraries.svg)](https://yuriolive.github.io/PortPS5/)
[![shaders](https://yuriolive.github.io/PortPS5/badge-shaders.svg)](https://yuriolive.github.io/PortPS5/)

Native Windows execution of decrypted PS5 game dumps.

PortPS5 converts **user-supplied, already-decrypted PS5 game dumps** into native Windows executables plus replacement system libraries. It executes the game's x86-64 code directly on the host CPU without emulation, relinks guest ELFs into PE images, implements PS5 system libraries (`.prx`), and translates the GPU workload to Vulkan.

PortPS5 is a GPL-2.0-only hard fork of [AnyPS5](https://github.com/boykopovar/AnyPS5) by boykopovar. Hard fork means an independent product direction, not isolation: improvements flow from upstream continuously, but each one is adopted selectively and adapted to fork policy — never taken as-is, and never depended on. Full upstream tracking is revisited only after Milestone 3 (see [PRD §5](docs/PRD.md)).

## Why a hard fork

Upstream optimizes for title-by-title progress, including game-specific mechanisms and environment switches for tuning and tracing. PortPS5 optimizes for a shippable, Windows-first product boundary instead: generic mechanisms only, enforced by CI; typed config instead of switches; return codes instead of throws; and a measured 1.0 bar (sections below). Upstream currently shows more public real-game evidence; the fork's arrives as results JSON from M2 onward.

## Product decisions

- **No title-specific code in `core/`.** Every title runs the same code. Per-title behaviour lives only in `config/games/<titleId>.toml` under `[workarounds]`, where the key names the mechanism (never the game) and is registered in [docs/workarounds.md](docs/workarounds.md). The hosted `policy` CI job enforces both directions.
- **No behaviour-changing environment variables.** The `APS5_*` switches are removed. Tracing, dumps and profiling survive only as a typed `[debug]` config section.
- **General mechanisms over game-specific fixes.** A fix that happens to help one title first (e.g. a uniform-fill or linear-copy IR pattern, GPU-side resource resolution, general block-generation write tracking) is adopted; a branch on a title ID, executable hash or shader hash is not. If a title cannot progress without a hack, work stops and the root cause goes into the subsystem spec's Open questions.
- **Honest errors.** Real POSIX/SCE conditions return codes. Only genuinely unsupported states abort, through the logging abort path. Nothing throws across the `APS5_VABI` boundary; every host function reachable from guest code is `APS5_VABI` (System V ABI).
- **Concurrency without global locks.** Guest synchronization uses in-place futex words on `WaitOnAddress`, not heap `std::` mutexes and no process-global lock on hot paths.
- **GPU work resolved on the GPU.** Indirect draws, buffer resolves at submit time, and write tracking run on GPU-side paths — never by reading records back on the CPU or satisfying a wait from an unexecuted label a capture depends on.
- **Verification splits by machine.** Hosted CI (build, unit, policy, python-quality, recompiler-golden — full job list in [spec/verification.md](docs/spec/verification.md); the `driver-lavapipe` job is planned M1 work, not running yet) has no GPU and never needs game data. GPU runs are local-only on a maintainer machine with their own dumps, and their only published output is metrics/hashes/pass-fail results JSON.

## Objectives — what 1.0 means

1.0 ships when all five pinned **gate titles** are completable start-to-credits and meet the performance bar on the generic reference tier. Full definitions, pins and exit criteria live in [PRD §4](docs/PRD.md) and [ROADMAP](docs/ROADMAP.md).

| # | Title | Tier | What it proves |
|---|---|---|---|
| 1 | Dreaming Sarah | 2D, custom | PM4 basics, blits, presentation, file streaming |
| 2 | TMNT: Shredder's Revenge | 2D action | Sprite throughput, pad input, audio mixing, 60 Hz pacing |
| 3 | Tomb Raider I-III Remastered | Simple 3D | Depth buffer, 3D transforms, texture sampling, save/load |
| 4 | Bugsnax | Unreal Engine 4 | UE4 job system, dynamic buffers, shadow passes |
| 5 | Demon's Souls | AAA custom engine | Async compute, resource aliasing, streaming, Bink FMV, indirect draws |

Functional bar (F1–F9): local CLI conversion with clear errors; save/load round-trip; audio including ATRAC9 with FMV A/V offset within ±80 ms and at most 1 underrun per 10 minutes; FMV must play (skipping is not a pass); XInput + DualSense-over-USB + keyboard/mouse; per-game TOML; warm disk pipeline cache (0 shader compilations, 0 pipeline creations during a regression pass); offline PSN/trophy behaviour that never blocks; runtime telemetry (frame-time log, watchdog, structured logs) feeding results JSON.

Performance bar (warm cache, 1920×1080 or higher, full run of at least 30 minutes of presented frames): average **≥30 fps**, **1% low ≥20 fps**, **0 crashes and 0 softlocks**. Stalls (>1 s between presents) are excluded from fps stats and counted separately; a softlock is no present or no guest-thread progress for >30 s, with no loading-screen exemption.

Reference tier (generic, benchmark-anchored): 12-core desktop CPU, mid-to-high-end Vulkan 1.3 GPU with 12 GB VRAM, 32 GB RAM, Windows 11 — qualified by Cinebench R23 multi-core ≥ 18,000 and 3DMark Time Spy graphics ≥ 18,000. No specific personal machine is part of the spec.

Post-1.0 non-goals: relink-time shader compilation, an AOB patch engine, upscaling/FPS targets (FSR, DLSS, 4K, 60/120 fps), Linux/macOS/GUI builds, DualSense haptics, titles beyond the gate set, and upstream tracking.

[![progress map](https://yuriolive.github.io/PortPS5/progress.svg)](https://yuriolive.github.io/PortPS5/)

<sub>* System libraries: percentage of the functions known to the project so far (declared in [core/libs/prx](core/libs/prx)), not of every PS5 system function. The total grows as more functions are declared. Rendered by `tools/progress.py` on every push to `main` (requires GitHub Pages enabled).</sub>

---

## How It Works

- **No CPU Emulation:** The PS5 CPU uses standard x86-64 (Zen 2). Game code runs natively at host speed.
- **ELF to PE Relinker (`core/relinker/`):** Patches guest ELFs into Windows PE binaries, resolving Sony NID imports to replacement library exports, preserving unwind info, and lowering unsupported instructions (such as SSE4a) when necessary.
- **PRX System Libraries (`core/libs/prx/`):** Clean-room replacement user-space libraries implementing POSIX, libc, libkernel, audio, and graphics APIs under the System V AMD64 ABI (`APS5_VABI`).
- **GPU Translation (`core/shader/` & driver):** RDNA2 command processor and compute/graphics shaders are translated to Vulkan and SPIR-V.

---

## Legal Boundary & Ethics

PortPS5 operates strictly within legal and ethical boundaries:

- **No Proprietary Code:** No Sony encryption keys, firmware, SDK headers, or proprietary libraries are included or distributed.
- **Decrypted Dumps Only:** PortPS5 does not decrypt games. Users must supply their own legally obtained, already-decrypted dumps.
- **Clean Submissions:** Issues, pull requests, and CI must never contain game assets, shader bytecode dumps, save files, or recorded game footage.

For complete rules, see [.agents/rules/legal-boundary.md](.agents/rules/legal-boundary.md).

---

## Documentation & Architecture

- **[PRD (Product Requirements Document)](docs/PRD.md):** 1.0 product goals, gate titles, performance bar, and reference hardware tier.
- **[Roadmap to 1.0](docs/ROADMAP.md):** Phased milestones (M0–M6) with measurable exit criteria.
- **[Subsystem Specifications](docs/spec/README.md):** Technical specs for relinker, shader recompiler, GPU driver, memory, threading, audio, input, and verification.
- **[Agent & Contributor Rules](AGENTS.md):** Canonical guide for developers and AI agents (Claude Code, opencode, Antigravity).
- **[Contributing Guide](CONTRIBUTING.md):** Community contribution workflow, pull requests, and verification.

---

## Toolchain & Requirements

- **OS:** Windows 10/11 (64-bit).
- **Compiler:** MinGW-w64 GCC 15.2 (ucrt-posix-seh). MSVC is not supported because it lacks `sysv_abi` function attribute support.
- **Build System:** CMake 3.20+ with CMakePresets.
- **Graphics API:** Vulkan 1.3 capable GPU and drivers.
- **Dependencies:** SDL2 (windowing, audio, input), Vulkan SDK.

See [docs/spec/build-toolchain.md](docs/spec/build-toolchain.md) for details on presets and building.

---

## Project Structure

```
core/relinker/        ELF -> PE relinker, NID resolution, --to-intel
core/libs/prx/<lib>/  Replacement system libraries (libc, libkernel, libSceAgcDriver, ...)
core/shader/          RDNA2 -> SSA IR -> SPIR-V recompiler
config/               global.toml and per-title overrides (config/games/<titleId>.toml)
docs/                 PRD, ROADMAP, subsystem specifications
seeds/                Milestone work plans for agentic workflows
.beans/               Task tracker (one markdown file per task; CLI `beans`, see AGENTS.md)
.agents/              Shared AI rules and skills
```

---

## Supporting the Project

PortPS5 is built by a solo maintainer with the help of AI agents, entirely in spare time.
Every sponsorship directly speeds up development:

| Your support goes towards | Why it matters |
|---|---|
| 🤖 **AI compute credits** | AI agents write code, review PRs, and run spec checks around the clock — but inference isn't free. More credits = faster iteration across all milestones. |
| 🖥️ **Test hardware** | Gate titles (Demon's Souls, Tomb Raider, Bugsnax …) need a Vulkan 1.3 GPU to validate. A second machine means continuous hardware-in-the-loop testing without blocking daily work. |
| ⚡ **Milestone velocity** | The [roadmap](docs/ROADMAP.md) spans M0 → M6. Sponsorship lets me dedicate more focused time to each milestone exit criterion instead of spreading it across weekends. |
| 📦 **Toolchain & CI costs** | Pinned MinGW GCC 15.2 builds, Vulkan SDK updates, and Windows CI runners all have ongoing overhead. |

If PortPS5 is useful or exciting to you, consider sponsoring — even a small monthly amount keeps the lights on and signals that the project is worth the effort.

[![Sponsor on GitHub](https://img.shields.io/badge/Sponsor-%E2%9D%A4-ea4aaa?style=for-the-badge&logo=githubsponsors)](https://github.com/sponsors/yuriolive)

---

## License

PortPS5 is licensed under the **GNU General Public License v2.0 only** ([GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)), preserving the license of the upstream AnyPS5 project.
