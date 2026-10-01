# Project Technical Debt

### Build

- [M0] Building on Windows requires a specific version of mingw — MinGW-w64 GCC 15.2.0 (`winlibs-gcc15`, `x86_64-ucrt-posix-seh`), pinned by CMake and CI checksum.

### Silent stubs

Throughout the project, every function at every stage either **does exactly what it's supposed to or throws an exception / logs and aborts**. Everywhere... except:
- [M1] [`Config::Loader::Initialize`](../core/libs/prx/libc/src/Config.cpp) has no production caller, so every config value is inert at runtime and consumers fall back to defaults (bean `portps5-c06p`).
- [M1] The present mode is hard-coded to FIFO ([VulkanDevice.cpp](../core/libs/prx/libSceAgcDriver/Execution/src/VulkanDevice.cpp) `:507`, `:584`) and the `display` keys are ignored (bean `portps5-dtwf`).

### Host exceptions

Host code still throws `std::` exceptions. Where a throw can reach an `APS5_VABI` export, the shared DWARF unwinder lets guest `catch(...)` swallow it, or a `noexcept` export turns it into `std::terminate`, instead of the logging abort path (`.agents/rules/cpp-style.md`). Counts of `throw std::` per module (`git grep -c "throw std::" -- core`, tests excluded, 2026-10-01):

| Module | Throws | Guest-reachable |
|---|---|---|
| `core/shader/recompiler` | 454 | Yes, through the AGC driver's compile path |
| `core/libs/prx/libc` | 160 | Yes |
| `core/libs/prx/libSceAgcDriver` | 111 | Yes |
| `core/libs/prx/libSceVideoOut` | 93 | Yes |
| `core/libs/prx/libkernel` | 79 | Yes |
| `core/libs/nid` | 62 | No (build-time NID patcher) |
| `core/libs/prx/libSceAgc` | 20 | Yes |
| `core/relinker/io`, `core/relinker/cli` | 12, 8 | No (relinker host tool) |
| `core/libs/prx/libSceSysmodule` | 9 | Yes |
| `core/libs/prx/libScePad` | 1 | Yes (`PadManager` rethrows a stored failure inside `noexcept` exports) |

### Unknown function info

- [M3] [zARR5aCmkoY](../core/libs/prx/libSceAgc/DcbFlow/src/Control.cpp) (libSceAgc) - link-level: exported by libSceAgc.prx (verified 2026-09-29 by export-table inspection); PortPS5 working name `sceAgcDcbA_zARR5aCmkoY`, Sony ABI name and signature unconfirmed
- [M3] [qj7QZpgr9Uw](../core/libs/prx/libSceAgc/DcbState/src/ContextState.cpp) (libSceAgc) - link-level: exported by libSceAgc.prx (verified 2026-09-29); PortPS5 working name `sceAgcDcbContextStateAnotherOp`, Sony ABI name unconfirmed
- [M3] [fd5Bp5tGTgo](../core/libs/prx/libSceAgc/Misc/src/ShaderFusion.cpp) (libSceAgc) - link-level: exported by libSceAgc.prx (verified 2026-09-29); PortPS5 working name `sceAgcUnknownFuseShaderHalves`, Sony ABI name unconfirmed
- [M3] [dolOmWH+huQ](../core/libs/prx/libSceAgc/Misc/src/ShaderFusion.cpp) (libSceAgc) - link-level: exported by libSceAgc.prx (verified 2026-09-29); PortPS5 working name `sceAgcUnknownGetFusedShaderSize`, Sony ABI name unconfirmed
- [M3] [V++UgBtQhn0](../core/libs/prx/libSceAgc/Misc/src/PacketInfo.cpp) (libSceAgc) - link-level: exported by libSceAgc.prx (verified 2026-09-29); PortPS5 working name `sceAgcGetDataPacketPayloadAddressUnk`, Sony ABI name unconfirmed
- [M3] [gQkqkLttcpw](../core/libs/prx/libSceAgc/Acb/src/Control.cpp) (libSceAgc) - link-level: exported by libSceAgc.prx (verified 2026-09-29); PortPS5 working name `sceAgcAcb_gQkqkLttcpw`, Sony ABI name and signature unconfirmed
- [M1] [sceKernelInternalMemoryGetModuleSegmentInfo](../core/libs/prx/libkernel/Module/src/Module.cpp) (libkernel) - unknown signature
- [M1] [sceLibcInternalBacktraceForGame](../core/libs/prx/libc/src/HeapDiagnostics.cpp) (libSceLibcInternal, implemented in libc) - unknown signature
- [M1] [sceLibcInternalHeapErrorReportForGame](../core/libs/prx/libc/src/HeapDiagnostics.cpp) (libSceLibcInternal, implemented in libc) - unknown signature
- [M1] [__progname](../core/libs/prx/libkernel/System/src/Process.cpp) (libkernel) - unknown data export

### Functional

- [M2] There's no way to specify keyboard and mouse input mapping when using a gamepad. The [default mapping](../core/libs/prx/libScePad/include/InputMapping.hpp) is always used. (bean `portps5-de24`)
- [Post-1.0] [Shader recompilation](../core/shader/recompiler/Recompiler.cpp) currently occurs right before it is transferred to Vulkan with caching. AOT recompile at relink time is deferred to post-1.0 (disk pipeline cache satisfies 1.0 stutter bar).
- [M6] The executable file that [relinker](../core/relinker/elfpatcher/src/windows/WindowsPeWriter.cpp) generates uses the console subsystem (CUI) by default, so it opens a console when launched; `--windows-gui` selects the GUI subsystem. Decide the release default.
- [M6] [Relinker](../core/relinker/elfpatcher/src) doesn't add an icon to the generated executable.
