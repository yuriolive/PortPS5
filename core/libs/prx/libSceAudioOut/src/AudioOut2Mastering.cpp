#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

// Mastering stays unimplemented in M1: no gate-title inventory entry needs it
// yet (see IMPORT_INVENTORY in this directory), so every entry aborts through
// the logging abort path instead of silently returning zeros.
int APS5_VABI sceAudioOut2MasteringInit(uint32_t flags) noexcept {
    (void)flags;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceAudioOut2MasteringTerm(void) noexcept {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceAudioOut2MasteringGetState(AudioOut2MasteringStatesHeader* state, uint32_t output, AudioOut2UserHandle user) noexcept {
    (void)state;
    (void)output;
    (void)user;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceAudioOut2MasteringSetParam(const AudioOut2MasteringParamsHeader* param, uint32_t output, uint32_t flags) noexcept {
    (void)param;
    (void)output;
    (void)flags;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
