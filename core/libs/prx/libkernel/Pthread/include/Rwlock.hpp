#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_RWLOCK_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_RWLOCK_HPP

#include "SceTypes.hpp"

extern "C" {

int APS5_VABI scePthreadRwlockattrInit(PthreadRwlockattr* attr) noexcept;
int APS5_VABI scePthreadRwlockattrDestroy(PthreadRwlockattr* attr) noexcept;
int APS5_VABI scePthreadRwlockattrSettype(PthreadRwlockattr* attr, int type) noexcept;
int APS5_VABI scePthreadRwlockInit(PthreadRwlock* rwlock, const PthreadRwlockattr* attr,
                                  const char* name) noexcept;
int APS5_VABI scePthreadRwlockDestroy(PthreadRwlock* rwlock) noexcept;
int APS5_VABI scePthreadRwlockRdlock(PthreadRwlock* rwlock) noexcept;
int APS5_VABI scePthreadRwlockTryrdlock(PthreadRwlock* rwlock) noexcept;
int APS5_VABI scePthreadRwlockWrlock(PthreadRwlock* rwlock) noexcept;
int APS5_VABI scePthreadRwlockTrywrlock(PthreadRwlock* rwlock) noexcept;
int APS5_VABI scePthreadRwlockUnlock(PthreadRwlock* rwlock) noexcept;

}

#endif
