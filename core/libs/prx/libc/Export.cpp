// libc.prx exports that are still unimplemented stubs or thin forwards to host helpers.
//
// Subsystem: libc.prx. Each function here is reached from guest code with the System V calling convention
// (APS5_VABI). Stubs report through NotImplemented_nid_no_patch, the logging abort path (they never return
// a fabricated success). Implemented exports live next to their family (RuntimeSupport.cpp for the exit and
// atexit registry and init_env, HeapExtras.cpp for operator new, and so on).
#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/HeapDiagnostics.hpp"

uint32_t Need_sceLibc = 1;

extern "C" {

    /// catchReturnFromMain: unimplemented stub (logging abort when hit); `status` is the main() return value.
    void APS5_VABI catchReturnFromMain_nid_postfix(int status) {
        (void)status;
        NotImplemented_nid_no_patch(__func__);
    }

    /// std::_Execute_once (call_once back end): unimplemented stub (logging abort when hit).
    int APS5_VABI std_execute_once_nid_postfix(int* flag, int (*func)(void*, void*, void**), void* arg) {
        (void)flag;
        (void)func;
        (void)arg;
        NotImplemented_nid_no_patch(__func__);
        return 0;
    }

    /// LibcHeapGetTraceInfo: fills `info` with the heap trace counters (see HeapDiagnostics).
    void APS5_VABI LibcHeapGetTraceInfo_nid_postfix(LibcHeapInfo* info) {
        LibcHeapTraceInfo_nid_no_patch(info);
    }

    /// LibcHeapErrorReportForGame: unimplemented stub (logging abort when hit); the arguments describe the
    /// failing mspace, pointer and error code the guest allocator detected.
    int APS5_VABI LibcHeapErrorReportForGame_nid_postfix(
        uint64_t msp, uint64_t ptr, uint64_t error,
        uint64_t arg3, uint64_t arg4, uint64_t arg5
    ) {
        (void)msp; (void)ptr; (void)error;
        (void)arg3; (void)arg4; (void)arg5;
        NotImplemented_nid_no_patch(__func__);
        return 0;
    }

}
