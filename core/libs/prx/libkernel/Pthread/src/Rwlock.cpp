#include "../include/Pthread.hpp"
#include "../include/Rwlock.hpp"
#include "prx/libkernel/Pthread/include/FutexCore.hpp"
#include "prx/libkernel/Pthread/include/GuestTid.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <cstdint>
#include <new>

namespace {

using SyncWords::kSceEagain;
using SyncWords::kSceEbusy;
using SyncWords::kSceEdeadlk;
using SyncWords::kSceEinval;
using SyncWords::kSceEperm;
using SyncWords::kSceOk;
using RW = SyncWords::RwlockWord;

inline std::uint64_t* WordPtr(PthreadRwlock* slot) noexcept {
    return reinterpret_cast<std::uint64_t*>(slot);
}
inline std::atomic_ref<std::uint64_t> WordRef(PthreadRwlock* slot) noexcept {
    return std::atomic_ref<std::uint64_t>(*WordPtr(slot));
}

// Writers are preferred once WRITERS_WAITING is set (spec): a new reader
// blocks when a writer holds the lock OR writers are queued, so a continuous
// reader stream cannot starve a writer. No global mutex; all state is the
// word + WaitOnAddress with re-check (spurious wakeups allowed).
int RdlockInternal(PthreadRwlock* slot, bool tryOnly) noexcept {
    if (!slot)
        return kSceEinval;
    if (GuestTid::Ensure() == 0)
        return kSceEagain;
    auto ref = WordRef(slot);
    while (true) {
        std::uint64_t w = ref.load(std::memory_order_acquire);
        if (w == 0 || w == 1) {
            const std::uint64_t init = RW::kInit;
            if (ref.compare_exchange_strong(w, init, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                w = init;
            else
                continue;
        }
        if (!RW::IsInit(w) || RW::IsDestroyed(w))
            return kSceEinval;
        if (RW::HasWriter(w) || RW::WritersWaiting(w)) {
            if (tryOnly)
                return kSceEbusy;
            // Queue as reader-waiter before sleeping (release) so unlock's
            // WakeAll cannot be lost; the re-check loop tolerates spurious.
            // Before sleeping, re-check that the lock is still held/queued:
            // if the writer left between flagging and loading (unlock raced
            // ahead and already returned), retry acquisition instead of
            // sleeping on a free word forever.
            if (!RW::ReadersWaiting(w)) {
                std::uint64_t expected = w;
                const std::uint64_t want = w | RW::kReadersWaiting;
                ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                            std::memory_order_acquire);
                w = ref.load(std::memory_order_acquire);
                if (!RW::IsInit(w) || RW::IsDestroyed(w))
                    return kSceEinval;
                if (!RW::HasWriter(w) && !RW::WritersWaiting(w)) {
                    // Writer left between our check and flag set; retry
                    // acquire immediately instead of sleeping.
                    continue;
                }
                FutexCore::WaitU64(WordPtr(slot), w, FutexCore::kInfinite);
                continue;
            } else {
                const std::uint64_t expect = ref.load(std::memory_order_acquire);
                if (!RW::IsInit(expect) || RW::IsDestroyed(expect))
                    return kSceEinval;
                if (!RW::HasWriter(expect) && !RW::WritersWaiting(expect))
                    continue;  // freed before we slept: retry, do not wait.
                FutexCore::WaitU64(WordPtr(slot), expect, FutexCore::kInfinite);
                continue;
            }
        }
        const std::uint32_t readers = RW::Readers(w);
        if (readers >= RW::kMaxReaders)
            return kSceEagain;  // saturates at 2^30.
        // Preserve waiter flags; clear READERS_WAITING only when we were the
        // last queued reader? We keep it set: a concurrent writer still sees
        // demand, and the next unlock wakes all. Clearing here would risk
        // starving writers that queued after us.
        const std::uint64_t want =
            (w & (RW::kInit | RW::kWriter | RW::kWritersWaiting | RW::kReadersWaiting |
                  RW::kWriterTidMask)) |
            (static_cast<std::uint64_t>(readers + 1) & RW::kReadersMask);
        std::uint64_t expected = w;
        if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
            return kSceOk;
    }
}

int WrlockInternal(PthreadRwlock* slot, bool tryOnly) noexcept {
    if (!slot)
        return kSceEinval;
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0)
        return kSceEagain;
    auto ref = WordRef(slot);
    while (true) {
        std::uint64_t w = ref.load(std::memory_order_acquire);
        if (w == 0 || w == 1) {
            const std::uint64_t init = RW::kInit;
            if (ref.compare_exchange_strong(w, init, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                w = init;
            else
                continue;
        }
        if (!RW::IsInit(w) || RW::IsDestroyed(w))
            return kSceEinval;
        if (RW::HasWriter(w) && RW::WriterTid(w) == tid)
            return kSceEdeadlk;  // writer relock.
        if (!RW::HasWriter(w) && RW::Readers(w) == 0) {
            const std::uint64_t want =
                RW::kInit | RW::kWriter |
                (static_cast<std::uint64_t>(tid) << RW::kWriterTidShift) |
                (w & (RW::kWritersWaiting | RW::kReadersWaiting));
            std::uint64_t expected = w;
            if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;
        }
        if (tryOnly)
            return kSceEbusy;
        if (!RW::WritersWaiting(w)) {
            std::uint64_t expected = w;
            ref.compare_exchange_strong(expected, w | RW::kWritersWaiting,
                                        std::memory_order_acq_rel, std::memory_order_acquire);
            w = ref.load(std::memory_order_acquire);
            if (!RW::IsInit(w) || RW::IsDestroyed(w))
                return kSceEinval;
            if (!RW::HasWriter(w) && RW::Readers(w) == 0)
                continue;  // lock freed between check and flag; retry.
        }
        const std::uint64_t expect = ref.load(std::memory_order_acquire);
        if (!RW::IsInit(expect) || RW::IsDestroyed(expect))
            return kSceEinval;
        // Lost-wakeup guard: if freed before we slept, retry instead of
        // waiting on a free word (unlock already returned, no wake coming).
        if (!RW::HasWriter(expect) && RW::Readers(expect) == 0)
            continue;
        FutexCore::WaitU64(WordPtr(slot), expect, FutexCore::kInfinite);
    }
}

}  // namespace

extern "C" {

int APS5_VABI scePthreadRwlockattrInit(PthreadRwlockattr* attr) noexcept {
    if (!attr)
        return kSceEinval;
    *attr = new (std::nothrow) PthreadRwlockattrPrivate{};
    return *attr ? kSceOk : SyncWords::kSceEnomem;
}

int APS5_VABI scePthreadRwlockattrDestroy(PthreadRwlockattr* attr) noexcept {
    if (!attr || !*attr)
        return kSceEinval;
    delete *attr;
    *attr = nullptr;
    return kSceOk;
}

int APS5_VABI scePthreadRwlockattrSettype(PthreadRwlockattr* attr, int type) noexcept {
    if (!attr || !*attr)
        return kSceEinval;
    (*attr)->_type = type;
    return kSceOk;
}

int APS5_VABI scePthreadRwlockInit(PthreadRwlock* rwlock, const PthreadRwlockattr* attr,
                                  const char* name) noexcept {
    (void)name;
    if (!rwlock)
        return kSceEinval;
    if (attr && !*attr)
        return kSceEinval;
    WordRef(rwlock).store(RW::kInit, std::memory_order_release);
    return kSceOk;
}

int APS5_VABI scePthreadRwlockDestroy(PthreadRwlock* rwlock) noexcept {
    if (!rwlock)
        return kSceEinval;
    auto ref = WordRef(rwlock);
    const std::uint64_t w = ref.load(std::memory_order_acquire);
    if (w == 0 || w == 1) {
        ref.store(RW::kDestroyedWord, std::memory_order_release);
        return kSceOk;
    }
    if (!RW::IsInit(w) || RW::IsDestroyed(w))
        return kSceEinval;
    if (RW::HasWriter(w) || RW::Readers(w) != 0)
        return kSceEbusy;
    ref.store(RW::kDestroyedWord, std::memory_order_release);
    return kSceOk;
}

int APS5_VABI scePthreadRwlockRdlock(PthreadRwlock* rwlock) noexcept {
    return RdlockInternal(rwlock, false);
}

int APS5_VABI scePthreadRwlockTryrdlock(PthreadRwlock* rwlock) noexcept {
    return RdlockInternal(rwlock, true);
}

int APS5_VABI scePthreadRwlockWrlock(PthreadRwlock* rwlock) noexcept {
    return WrlockInternal(rwlock, false);
}

int APS5_VABI scePthreadRwlockTrywrlock(PthreadRwlock* rwlock) noexcept {
    return WrlockInternal(rwlock, true);
}

int APS5_VABI scePthreadRwlockUnlock(PthreadRwlock* rwlock) noexcept {
    if (!rwlock)
        return kSceEinval;
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0)
        return kSceEagain;
    auto ref = WordRef(rwlock);
    while (true) {
        const std::uint64_t w = ref.load(std::memory_order_acquire);
        if (w == 0 || w == 1 || !RW::IsInit(w) || RW::IsDestroyed(w))
            return kSceEinval;
        if (RW::HasWriter(w)) {
            if (RW::WriterTid(w) != tid)
                return kSceEperm;
            // Write unlock: clear WRITER|tid, keep waiter flags for the
            // wake decision, then wake all when anyone queued.
            const bool queued = RW::WritersWaiting(w) || RW::ReadersWaiting(w);
            const std::uint64_t want =
                RW::kInit | (w & (RW::kWritersWaiting | RW::kReadersWaiting));
            std::uint64_t expected = w;
            if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
                // Clear stale waiter flags now that the lock is free: the
                // woken threads re-queue if they still must wait, so no
                // wakeup is lost and flags do not stick forever.
                std::uint64_t cur = ref.load(std::memory_order_acquire);
                while ((cur & (RW::kWritersWaiting | RW::kReadersWaiting)) != 0) {
                    std::uint64_t exp2 = cur;
                    const std::uint64_t cleared =
                        cur & ~(RW::kWritersWaiting | RW::kReadersWaiting);
                    if (ref.compare_exchange_strong(exp2, cleared, std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
                        break;
                    cur = exp2;
                }
                if (queued)
                    FutexCore::WakeAll(WordPtr(rwlock));
                return kSceOk;
            }
            continue;
        }
        const std::uint32_t readers = RW::Readers(w);
        if (readers == 0)
            return kSceEperm;  // unlock of an unlocked rwlock.
        const std::uint64_t want =
            (w & (RW::kInit | RW::kWritersWaiting | RW::kReadersWaiting | RW::kWriterTidMask)) |
            (static_cast<std::uint64_t>(readers - 1) & RW::kReadersMask);
        const bool lastReader = (readers == 1);
        const bool queued = RW::WritersWaiting(w) || RW::ReadersWaiting(w);
        std::uint64_t expected = w;
        if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            if (lastReader && queued) {
                // Last reader out with waiters: clear flags (woken sides
                // re-queue) and wake all; writer preference is enforced by
                // the readers re-checking WRITERS_WAITING before acquiring.
                std::uint64_t cur = ref.load(std::memory_order_acquire);
                while ((cur & (RW::kWritersWaiting | RW::kReadersWaiting)) != 0) {
                    std::uint64_t exp2 = cur;
                    const std::uint64_t cleared =
                        cur & ~(RW::kWritersWaiting | RW::kReadersWaiting);
                    if (ref.compare_exchange_strong(exp2, cleared, std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
                        break;
                    cur = exp2;
                }
                FutexCore::WakeAll(WordPtr(rwlock));
            }
            return kSceOk;
        }
    }
}

}
