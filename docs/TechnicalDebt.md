# Project Technical Debt

### Build

- [M0] Building on Windows requires a specific version of mingw — MinGW-w64 GCC 15.2.0 (`winlibs-gcc15`, `x86_64-ucrt-posix-seh`), pinned by CMake and CI checksum.
- [M0] PRX libraries on Windows require the runtime DLLs copied nearby (static linking causes conflicts):
  - `libgcc_s_seh-1.dll`
  - `libstdc++-6.dll`
  - `libwinpthread-1.dll`
  (Resolved in M0 via CMake `libs` target POST_BUILD copy step).

### Silent stubs

Throughout the project, every function at every stage either **does exactly what it's supposed to or throws an exception / logs and aborts**. Everywhere... except:
- [M2] [libSceSaveDataDialog.native](../core/libs/prx/libSceSaveDataDialog.native/Export.cpp)
- [M1] [libSceCommonDialog](../core/libs/prx/libSceCommonDialog/Export.cpp)
- [M3] The shader recompiler [skips barycentric coordinates](../core/shader/recompiler/Recompiler.cpp) (is not even passed to SpirvTargetOptions).

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

- [M2] There's no way to specify keyboard and mouse input mapping when using a gamepad. The [default mapping](../core/libs/prx/libScePad/include/InputMapping.hpp) is always used.
- [Post-1.0] [Shader recompilation](../core/shader/recompiler/Recompiler.cpp) currently occurs right before it is transferred to Vulkan with caching. AOT recompile at relink time is deferred to post-1.0 (disk pipeline cache satisfies 1.0 stutter bar).
- [M6] The executable file that [relinker](../core/relinker/elfpatcher/src/windows/WindowsPeWriter.cpp) generates opens the console when launched.
- [M6] [Relinker](../core/relinker/elfpatcher/src) doesn't add an icon to the generated executable.
