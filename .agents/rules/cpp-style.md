# C++ conventions

These rules come from the AnyPS5 `docs/CONVENTIONS.md`, with PortPS5's changes.

- **Standard:** C++20 today and C++23 from Milestone 0. The only compiler is MinGW-w64 GCC 15.2. Don't use MSVC-only or clang-only features unless they are behind a guard.
- **Naming:** PascalCase for types and functions, `I`-prefixed interfaces, `T`-prefixed template parameters. Match the surrounding file.
- **Documentation & Comments (Enforced Best Practice):**
  - **Every new file** must begin with a file-level comment header explaining its purpose, subsystem ownership, lifecycle, and threading invariants.
  - **Every class, struct, and public function** must have doc-comments documenting parameters, return values, SCE/POSIX error codes, and thread-safety constraints.
  - **Inline comments** are mandatory for non-obvious logic: magic constants, hand-assembled bytes, ABI tricks, unwinding constraints (`APS5_VABI` / `sysv_abi` vs Windows SEH, DWARF `.eh_frame -> .ehfram` renaming), memory barriers, atomic operations, and synchronization invariants.
  - **Test cases (`TEST`, `TEST_F`)** must include comments explaining the exact behavioral invariant being verified, test preconditions, and expected failure modes.
  - Undocumented workarounds or silent hacks are forbidden. When working around platform or compiler quirks, document the exact rationale and reference the relevant spec in `docs/spec/`.
  - Maintain standardization: follow consistent naming, explicit types, and zero warnings under `-Werror`.
- **ABI:**
  - Every host function reachable from guest code uses `APS5_VABI` (System V calling convention).
  - Guest-visible structs use explicit-width types and `static_assert` on their size and offsets.
- **Errors:**
  - A POSIX or SCE error that a real console would return is returned as a code.
  - A state that is genuinely unsupported goes through the logging abort path. Don't add new `throw std::runtime_error`, because the shared unwinder lets guest `catch(...)` swallow host exceptions.
- **Concurrency:**
  - Guest synchronization uses in-place futex words on `WaitOnAddress`, not heap-allocated `std::` mutexes.
  - No process-global locks on hot paths.
- **Guest memory:**
  - Treat all guest pointers as untrusted and validate them through the guest-memory API.
  - Never hand guest code host addresses outside the guest arena.
- **Dependencies:** add them as pinned submodules under `3rdparty/` with a compatible licence (see `legal-boundary.md`).
