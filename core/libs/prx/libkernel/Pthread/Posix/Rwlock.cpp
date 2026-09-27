#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "../include/Rwlock.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"

extern "C" {

int APS5_VABI pthread_rwlock_destroy_nid_postfix(PthreadRwlock* rwlock) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockDestroy(rwlock));
}

int APS5_VABI pthread_rwlock_init_nid_postfix(PthreadRwlock* rwlock,
                                              const PthreadRwlockattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockInit(rwlock, attr, nullptr));
}

int APS5_VABI pthread_rwlock_rdlock_nid_postfix(PthreadRwlock* rwlock) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockRdlock(rwlock));
}

int APS5_VABI pthread_rwlock_tryrdlock_nid_postfix(PthreadRwlock* rwlock) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockTryrdlock(rwlock));
}

int APS5_VABI pthread_rwlock_trywrlock_nid_postfix(PthreadRwlock* rwlock) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockTrywrlock(rwlock));
}

int APS5_VABI pthread_rwlock_unlock_nid_postfix(PthreadRwlock* rwlock) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockUnlock(rwlock));
}

int APS5_VABI pthread_rwlock_wrlock_nid_postfix(PthreadRwlock* rwlock) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockWrlock(rwlock));
}

int APS5_VABI pthread_rwlockattr_destroy_nid_postfix(PthreadRwlockattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockattrDestroy(attr));
}

int APS5_VABI pthread_rwlockattr_init_nid_postfix(PthreadRwlockattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockattrInit(attr));
}

int APS5_VABI pthread_rwlockattr_settype_np_nid_postfix(PthreadRwlockattr* attr, int type) noexcept {
    return SyncWords::ToPosix(scePthreadRwlockattrSettype(attr, type));
}

}
