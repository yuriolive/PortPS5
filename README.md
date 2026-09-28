# PortPS5

[![GitHub Sponsors](https://img.shields.io/github/sponsors/yuriolive?style=flat-square)](https://github.com/sponsors/yuriolive)

Native Windows execution of decrypted PS5 game dumps.

PortPS5 converts **user-supplied, already-decrypted PS5 game dumps** into native Windows executables plus replacement system libraries. It executes the game's x86-64 code directly on the host CPU without emulation, relinks guest ELFs into PE images, implements PS5 system libraries (`.prx`), and translates the GPU workload to Vulkan.

PortPS5 is a GPL-2.0-only hard fork of [AnyPS5](https://github.com/boykopovar/AnyPS5) by boykopovar.

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

## License

PortPS5 is licensed under the **GNU General Public License v2.0 only** ([GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)), preserving the license of the upstream AnyPS5 project.
