// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_PTHREAD_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_PTHREAD_HPP

// M1 threading slice: guest sync objects are in-place futex words, not host
// pointers (docs/spec/threading.md Target design). The 8-byte guest slot IS
// a MutexWord/CondWord/RwlockWord (see SyncWords.hpp); this header only keeps
// the small attr structs and the host-side thread handle. No process-global
// lock lives here: hot paths use CAS + WaitOnAddress only.

#include <sched.h>
#include "SceTypes.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

enum class MutexType : std::uint32_t {
    ErrorCheck = 1,
    Recursive = 2,
    Normal = 3,
};

struct PthreadMutexattrPrivate {
    MutexType type;
};

struct PthreadMutexPrivate {
    // Unused in the futex design: the guest slot holds the word directly.
    // Kept so old sizeof assumptions fail loudly if anything dereferences it.
    std::uint64_t _reserved = 0;
};

struct PthreadCondattrPrivate {
    int _clockid;
};

struct PthreadCondPrivate {
    std::uint64_t _reserved = 0;
};

struct PthreadRwlockPrivate {
    std::uint64_t _reserved = 0;
};

struct PthreadRwlockattrPrivate {
    int _type = 0;
};

struct PthreadAttrPrivate {
    void* stackAddress = nullptr;
    std::size_t _stacksize;
    int _detachstate;
    int _schedpriority;
    int _schedpolicy;
    int _inheritsched;
    // Recorded, never applied: guest masks name console cores, not host
    // cores (threading.md: affinity recorded, not applied; this spec owns it).
    std::uint64_t _affinity = 0;
    std::size_t _guardsize = 4096;
};

struct PthreadPrivate {
#ifdef _WIN32
    void* nativeHandle = nullptr;
    std::thread::id threadId;
    std::atomic<unsigned> references{2};
#else
    std::thread _thr;
#endif
    void* stackAddress = nullptr;
    std::size_t stackSize = 0;
    std::size_t guardSize = 4096;
    std::atomic<bool> _finished;
    void* _retval;
    std::atomic<bool> _detached;
    std::mutex _join_mtx;
    std::condition_variable _join_cv;
    // Compact guest tid in [1, 2^24), allocated at thread entry and recycled
    // after join (joinable) or on exit (detached). It is the owner field in
    // every futex word and the value scePthreadGetthreadid returns.
    std::uint32_t guestTid = 0;
    // True for the lazily created handle of a host thread that never went
    // through scePthreadCreate (the guest main thread, driver workers). Such a
    // handle is detached (join/detach fail with EINVAL), lives in
    // HostThreadLocal (FLS) storage freed at host-thread exit (see
    // scePthreadSelf), and is never reference-counted or
    // CloseHandle'd; its tid belongs to GuestTid's per-thread cleanup.
    bool adopted = false;
    // Recorded, never applied (see above).
    std::atomic<std::uint64_t> affinityMask = 0;
    std::atomic<int> schedPriority = 700;
    char threadName[32] = {};
    std::mutex _name_mtx;

    PthreadPrivate() : _finished(false), _retval(nullptr), _detached(false) {}
};

#endif
