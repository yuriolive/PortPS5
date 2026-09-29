<p align="center">
  <img src="assets/logo.png" alt="PortPS5 Logo" width="180" />
</p>

# PortPS5

[![CI](https://img.shields.io/github/actions/workflow/status/yuriolive/PortPS5/ci.yml?branch=main&label=CI&style=flat-square)](https://github.com/yuriolive/PortPS5/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/yuriolive/PortPS5/codeql.yml?branch=main&label=CodeQL&style=flat-square)](https://github.com/yuriolive/PortPS5/actions/workflows/codeql.yml)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/yuriolive/PortPS5?style=flat-square&utm_source=oss&utm_medium=github&utm_campaign=yuriolive%2FPortPS5&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0--only-blue?style=flat-square)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows%2010%2F11-informational?style=flat-square&logo=windows)](https://github.com/yuriolive/PortPS5)
[![Language: C++23](https://img.shields.io/badge/language-C%2B%2B23-informational?style=flat-square&logo=cplusplus)](https://github.com/yuriolive/PortPS5)
[![Vulkan 1.3](https://img.shields.io/badge/Vulkan-1.3-red?style=flat-square&logo=vulkan)](https://www.vulkan.org/)
[![GitHub Sponsors](https://img.shields.io/github/sponsors/yuriolive?style=flat-square&logo=githubsponsors&color=ea4aaa)](https://github.com/sponsors/yuriolive)
[![libraries](https://yuriolive.github.io/PortPS5/badge-libraries.svg)](https://yuriolive.github.io/PortPS5/)
[![shaders](https://yuriolive.github.io/PortPS5/badge-shaders.svg)](https://yuriolive.github.io/PortPS5/)

Native Windows execution of decrypted PS5 game dumps.

PortPS5 converts **user-supplied, already-decrypted PS5 game dumps** into native Windows executables plus replacement system libraries. It executes the game's x86-64 code directly on the host CPU without emulation, relinks guest ELFs into PE images, implements PS5 system libraries (`.prx`), and translates the GPU workload to Vulkan.

PortPS5 is a GPL-2.0-only hard fork of [AnyPS5](https://github.com/boykopovar/AnyPS5) by boykopovar.

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
