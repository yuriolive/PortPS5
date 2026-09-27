#include "prx/libkernel/Pthread/include/Cond.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include <cstdint>

extern "C" {

int APS5_VABI pthread_cond_broadcast_nid_postfix(PthreadCond* cond) noexcept {
    return SyncWords::ToPosix(scePthreadCondBroadcast(cond));
}

int APS5_VABI pthread_cond_init_nid_postfix(PthreadCond* cond,
                                            const PthreadCondattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadCondInit(cond, attr, nullptr));
}

int APS5_VABI pthread_cond_signal_nid_postfix(PthreadCond* cond) noexcept {
    return SyncWords::ToPosix(scePthreadCondSignal(cond));
}

int APS5_VABI pthread_cond_timedwait_nid_postfix(PthreadCond* cond, PthreadMutex* mutex,
                                                 const KernelTimespec* abstime) noexcept {
    return SyncWords::ToPosix(CondOperations::AbsoluteTimedwait(cond, mutex, abstime));
}

int APS5_VABI pthread_cond_wait_nid_postfix(PthreadCond* cond, PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadCondWait(cond, mutex));
}

int APS5_VABI pthread_condattr_destroy_nid_postfix(PthreadCondattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadCondattrDestroy(attr));
}

int APS5_VABI pthread_condattr_init_nid_postfix(PthreadCondattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadCondattrInit(attr));
}

int APS5_VABI pthread_condattr_setclock_nid_postfix(PthreadCondattr* attr,
                                                    KernelClockid clock_id) noexcept {
    return SyncWords::ToPosix(scePthreadCondattrSetclock(attr, clock_id));
}

}
