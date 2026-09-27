# Contributing to PortPS5

Thank you for your interest in contributing to PortPS5!

PortPS5 is a clean-room, native Windows conversion layer for decrypted PS5 game dumps. To maintain legality, reproducibility, and code quality, all contributors must follow these guidelines.

---

## 1. Legal & Ethical Boundaries (Zero Tolerance)

The following must **NEVER** enter this repository, pull requests, issues, or commit histories:
* **No Sony proprietary material:** No encryption keys, firmware blobs, official SDK headers, or proprietary Sony libraries.
* **No game assets:** No decrypted or encrypted dumps, shader bytecode dumps, save files, or recorded game footage/audio.
* **Clean-room implementations only:** All PRX replacements (`core/libs/prx/`) must be reverse-engineered or clean-room implemented without copying copyrighted code.

For complete rules, see [.agents/rules/legal-boundary.md](.agents/rules/legal-boundary.md).

---

## 2. Getting Started & Development

* **Platform:** Windows 10/11 (64-bit).
* **Compiler:** MinGW-w64 GCC 15.2 (ucrt-posix-seh). MSVC is unsupported due to lacking System V ABI attributes (`sysv_abi`).
* **Build System:** CMake 3.20+ with CMakePresets.
* **Toolchain setup & build instructions:** See [docs/spec/build-toolchain.md](docs/spec/build-toolchain.md).

### Local Verification Before Submitting
Always configure, compile, and run tests locally before opening or updating a PR:
```powershell
cmake --preset ci
cmake --build --preset ci
ctest --preset ci
```
Do not rely on hosted CI as a syntax or build checker.

---

## 3. Pull Request & Git Workflow

1. **Fork & Branch:**
   * Fork the repository to your GitHub account.
   * Create a branch from `main` using descriptive naming: `feat/<name>`, `fix/<name>`, `docs/<name>`, or `chore/<name>`.
2. **Conventional Commits:**
   * Format commit messages with conventional types: `feat(relinker): ...`, `fix(libkernel): ...`, `docs(spec): ...`.
   * Subject line max 72 characters; commit body explains the *why*.
3. **CI & Maintainer Approval:**
   * Pull requests from outside forks require one-time approval from code owners before GitHub Actions workflows run.
   * All required checks (`Build & Test`, `CodeQL`, `Gitleaks`) must pass.
   * `main` is protected: PRs require approval from code owners (`@yuriolive`), linear history, and all review threads resolved before merge.

---

## 4. Subsystem Specifications & Documentation

* PortPS5 follows a specification-first methodology. If your change alters subsystem behaviour or adds new capabilities, update the relevant specification under `docs/spec/` in the same PR.
* See [docs/spec/README.md](docs/spec/README.md) for subsystem architectures.
