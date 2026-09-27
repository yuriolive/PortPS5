#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_MUTEX_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_MUTEX_HPP

#include "SceTypes.hpp"

class MutexOperations {
public:
    static int Timedlock(PthreadMutex* mutex, const KernelTimespec* abstime);
};

extern "C" {

int APS5_VABI scePthreadMutexattrInit(PthreadMutexattr* attr) noexcept;
int APS5_VABI scePthreadMutexattrDestroy(PthreadMutexattr* attr) noexcept;
int APS5_VABI scePthreadMutexattrSettype(PthreadMutexattr* attr, int type) noexcept;
int APS5_VABI scePthreadMutexattrSetprotocol(PthreadMutexattr* attr, int protocol) noexcept;
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr,
                                 const char* name) noexcept;
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex) noexcept;
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept;
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept;
int APS5_VABI scePthreadMutexTimedlock(PthreadMutex* mutex, KernelUseconds usec) noexcept;
int APS5_VABI scePthreadMutexTrylock(PthreadMutex* mutex) noexcept;

}

#endif
