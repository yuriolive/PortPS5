---
name: implement-prx-function
description: Implement or fix a PS5 system-library (prx) function in PortPS5 (NID export, System V ABI, return codes, test). Use when a title calls an unimplemented or wrong libkernel/libSce* function.
---

# Implement a prx function

1. **Locate** the library under `core/libs/prx/<lib>/`. Find how neighbouring exports are declared (the export macros and `APS5_VABI`) and copy that pattern exactly.
2. **Establish the behaviour** only from allowed sources (`.agents/rules/legal-boundary.md`):
   - the function's observed use in the user's own dump;
   - open-source implementations (AnyPS5, shadPS4, FreeBSD for POSIX).

   Never use Sony SDK material.
3. **Signature:**
   - Explicit-width types, with `static_assert` on the size and offsets of any struct the guest can see.
   - Every host entry point reachable from the guest is `APS5_VABI`.
4. **Errors:**
   - Return the SCE/POSIX error code a console would return.
   - A genuinely unsupported argument or state goes through the logging abort path, not a silent stub.
   - If a stub is unavoidable, log it once and list it in `docs/TechnicalDebt.md`.
5. **Title-agnostic:** no title-specific branches (`.agents/rules/no-title-hacks.md`).
6. **Test:** add a unit test for the success path and each error code, wired into ctest.
7. **Spec:** if the function changes subsystem behaviour, update the relevant `docs/spec/*.md` with the `update-spec` skill.
