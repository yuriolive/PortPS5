// libSceLibcInternal exports. The module shares libc's runtime state: it forwards to libc's host helpers
// instead of keeping a second registry.
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/HeapDiagnostics.hpp"

extern "C" {

/// `__cxa_finalize` as exported by libSceLibcInternal; runs the handlers libc registered for `dsoHandle`
/// (null runs all). Forwards to CxaFinalize_nid_no_patch so both modules see one registry.
void APS5_VABI __cxa_finalize_nid_postfix(void* dsoHandle) {
    CxaFinalize_nid_no_patch(dsoHandle);
}

/// sceLibcHeapGetTraceInfo: fills `info` with the heap trace counters (same data as libc's export).
void APS5_VABI sceLibcHeapGetTraceInfo_nid_postfix(Info* info) {
    LibcHeapTraceInfo_nid_no_patch(info);
}

}
