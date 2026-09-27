#include "../include/Mutex.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include <cstdint>

extern "C" {

int APS5_VABI pthread_mutex_destroy_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexDestroy(mutex));
}

int APS5_VABI pthread_mutex_init_nid_postfix(PthreadMutex* mutex,
                                             const PthreadMutexattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadMutexInit(mutex, attr, nullptr));
}

int APS5_VABI pthread_mutex_lock_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexLock(mutex));
}

int APS5_VABI pthread_mutex_timedlock_nid_postfix(PthreadMutex* mutex,
                                                  const KernelTimespec* abstime) noexcept {
    return SyncWords::ToPosix(MutexOperations::Timedlock(mutex, abstime));
}

int APS5_VABI pthread_mutex_trylock_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexTrylock(mutex));
}

int APS5_VABI pthread_mutex_unlock_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexUnlock(mutex));
}

int APS5_VABI pthread_mutexattr_destroy_nid_postfix(PthreadMutexattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrDestroy(attr));
}

int APS5_VABI pthread_mutexattr_init_nid_postfix(PthreadMutexattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrInit(attr));
}

int APS5_VABI pthread_mutexattr_setprotocol_nid_postfix(PthreadMutexattr* attr,
                                                        int protocol) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrSetprotocol(attr, protocol));
}

int APS5_VABI pthread_mutexattr_settype_nid_postfix(PthreadMutexattr* attr, int type) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrSettype(attr, type));
}

}
