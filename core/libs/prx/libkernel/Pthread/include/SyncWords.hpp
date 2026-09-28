#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_SYNCWORDS_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_SYNCWORDS_HPP

// In-place futex word layouts (docs/spec/threading.md Target design).
// Why in place: the whole 8-byte guest slot IS the word, accessed with
// 64-bit atomics. Canonical user pointers never set bit 63, so INIT words
// cannot collide with the old pointer representation (SceTypes.hpp slots are
// 8 bytes). Slot == 0 is the static default (ErrorCheck); slot == 1 is the
// adaptive static (Normal). All helpers use explicit-width types; sizes and
// offsets are static_asserted so guest-visible layout changes fail loudly.
// Ordering: every mutation is a CAS loop with acquire on load and release on
// success; waiters set CONTENDED/waiters BEFORE sleeping (release) and the
// waker publishes the new word BEFORE waking (release), so no wakeup is lost.

#include <cstddef>
#include <cstdint>

namespace SyncWords {

// --- SCE errno carriers (FreeBSD low 16 bits under 0x80020000) ---
inline constexpr int kSceOk = 0;
inline constexpr int kSceEinval = static_cast<int>(0x80020016u);    // 22
inline constexpr int kSceEbusy = static_cast<int>(0x80020010u);     // 16
inline constexpr int kSceTimedOut = static_cast<int>(0x8002003Cu);  // 60
inline constexpr int kSceEdeadlk = static_cast<int>(0x8002000Bu);   // 11
inline constexpr int kSceEperm = static_cast<int>(0x80020001u);     // 1
inline constexpr int kSceEagain = static_cast<int>(0x80020023u);    // 35
inline constexpr int kSceEsrch = static_cast<int>(0x80020003u);     // 3
inline constexpr int kSceEnomem = static_cast<int>(0x8002000Cu);    // 12
inline constexpr int kSceEownerdead = static_cast<int>(0x80020060u);    // 96
inline constexpr int kSceEnotrecoverable = static_cast<int>(0x8002005Fu); // 95

// POSIX (FreeBSD) errnos returned by the pthread_* wrappers.
inline constexpr int kPosixEinval = 22;
inline constexpr int kPosixEbusy = 16;
inline constexpr int kPosixTimedOut = 60;
inline constexpr int kPosixEdeadlk = 11;
inline constexpr int kPosixEperm = 1;
inline constexpr int kPosixEagain = 35;
inline constexpr int kPosixEsrch = 3;
inline constexpr int kPosixEnomem = 12;

// Strip an SCE carrier to its POSIX low bits. Returns kPosixEinval for any
// unexpected carrier (never throws: noexcept boundary).
inline int ToPosix(int sce) noexcept {
    if (sce == kSceOk)
        return 0;
    const auto u = static_cast<std::uint32_t>(sce);
    if ((u & 0xFFFF0000u) == 0x80020000u)
        return static_cast<int>(u & 0xFFFFu);
    return kPosixEinval;
}

// ---------------- Mutex word ----------------
// bit 63 INIT | 62 DESTROYED | 58..56 type (1 errchk, 2 recursive, 3 normal)
//     | 47..32 recursion-1 | 31 CONTENDED | 23..0 owner tid
struct MutexWord {
    using Storage = std::uint64_t;
    static constexpr Storage kInit = 1ULL << 63;
    static constexpr Storage kDestroyed = 1ULL << 62;
    static constexpr unsigned kTypeShift = 56;
    static constexpr Storage kTypeMask = 0x7ULL << kTypeShift;
    static constexpr unsigned kRecShift = 32;
    static constexpr Storage kRecMask = 0xFFFFULL << kRecShift;
    static constexpr Storage kContended = 1ULL << 31;
    static constexpr Storage kOwnerMask = 0xFFFFFFULL;
    static constexpr std::uint32_t kErrCheck = 1u;
    static constexpr std::uint32_t kRecursive = 2u;
    static constexpr std::uint32_t kNormal = 3u;
    // Destroyed marker keeps INIT (never collides with pointers/0/1/2).
    static constexpr Storage kDestroyedWord = kInit | kDestroyed;

    static constexpr Storage Make(std::uint32_t type, std::uint32_t owner, std::uint32_t recStored,
                                  bool contended) noexcept {
        Storage w = kInit | (static_cast<Storage>(type & 0x7u) << kTypeShift) |
                    (static_cast<Storage>(recStored) << kRecShift) |
                    (static_cast<Storage>(owner) & kOwnerMask);
        if (contended)
            w |= kContended;
        return w;
    }
    static constexpr bool IsInit(Storage w) noexcept { return (w & kInit) != 0; }
    static constexpr bool IsDestroyed(Storage w) noexcept { return (w & kDestroyed) != 0; }
    static constexpr std::uint32_t Type(Storage w) noexcept {
        return static_cast<std::uint32_t>((w & kTypeMask) >> kTypeShift);
    }
    static constexpr std::uint32_t RecStored(Storage w) noexcept {
        return static_cast<std::uint32_t>((w & kRecMask) >> kRecShift);
    }
    static constexpr std::uint32_t Owner(Storage w) noexcept {
        return static_cast<std::uint32_t>(w & kOwnerMask);
    }
    static constexpr bool IsContended(Storage w) noexcept { return (w & kContended) != 0; }
};
static_assert(sizeof(MutexWord::Storage) == 8);

// ---------------- Cond word ----------------
// bit 63 INIT | 62 DESTROYED | 61 CLOCK_MONOTONIC | 60..48 waiters | 47..0 seq
struct CondWord {
    using Storage = std::uint64_t;
    static constexpr Storage kInit = 1ULL << 63;
    static constexpr Storage kDestroyed = 1ULL << 62;
    static constexpr Storage kClockMono = 1ULL << 61;
    static constexpr unsigned kWaitersShift = 48;
    static constexpr Storage kWaitersMask = 0x1FFFULL << kWaitersShift;
    static constexpr Storage kSeqMask = 0xFFFFFFFFFFFFULL;
    static constexpr Storage kDestroyedWord = kInit | kDestroyed;

    static constexpr Storage Make(bool mono, std::uint32_t waiters, std::uint64_t seq) noexcept {
        Storage w = kInit | ((static_cast<Storage>(waiters) & 0x1FFFULL) << kWaitersShift) |
                    (seq & kSeqMask);
        if (mono)
            w |= kClockMono;
        return w;
    }
    static constexpr bool IsInit(Storage w) noexcept { return (w & kInit) != 0; }
    static constexpr bool IsDestroyed(Storage w) noexcept { return (w & kDestroyed) != 0; }
    static constexpr bool IsMono(Storage w) noexcept { return (w & kClockMono) != 0; }
    static constexpr std::uint32_t Waiters(Storage w) noexcept {
        return static_cast<std::uint32_t>((w & kWaitersMask) >> kWaitersShift);
    }
    static constexpr std::uint64_t Seq(Storage w) noexcept {
        return w & kSeqMask;
    }
};
static_assert(sizeof(CondWord::Storage) == 8);

// ---------------- Rwlock word ----------------
// 63 INIT | 62 DESTROYED | 61 WRITER | 60 WRITERS_WAITING | 59 READERS_WAITING
//     | 55..32 writer tid | 29..0 readers
struct RwlockWord {
    using Storage = std::uint64_t;
    static constexpr Storage kInit = 1ULL << 63;
    static constexpr Storage kDestroyed = 1ULL << 62;
    static constexpr Storage kWriter = 1ULL << 61;
    static constexpr Storage kWritersWaiting = 1ULL << 60;
    static constexpr Storage kReadersWaiting = 1ULL << 59;
    static constexpr unsigned kWriterTidShift = 32;
    static constexpr Storage kWriterTidMask = 0xFFFFFFULL << kWriterTidShift;
    static constexpr Storage kReadersMask = 0x3FFFFFFFULL;  // 30 bits.
    static constexpr std::uint32_t kMaxReaders = (1u << 30) - 1;  // saturates, then EAGAIN.
    static constexpr Storage kDestroyedWord = kInit | kDestroyed;

    static constexpr Storage Make(bool writer, std::uint32_t writerTid, std::uint32_t readers,
                                  bool ww, bool rw) noexcept {
        Storage w = kInit | (static_cast<Storage>(readers) & kReadersMask) |
                    ((static_cast<Storage>(writerTid) & 0xFFFFFFULL) << kWriterTidShift);
        if (writer)
            w |= kWriter;
        if (ww)
            w |= kWritersWaiting;
        if (rw)
            w |= kReadersWaiting;
        return w;
    }
    static constexpr bool IsInit(Storage w) noexcept { return (w & kInit) != 0; }
    static constexpr bool IsDestroyed(Storage w) noexcept { return (w & kDestroyed) != 0; }
    static constexpr bool HasWriter(Storage w) noexcept { return (w & kWriter) != 0; }
    static constexpr bool WritersWaiting(Storage w) noexcept { return (w & kWritersWaiting) != 0; }
    static constexpr bool ReadersWaiting(Storage w) noexcept { return (w & kReadersWaiting) != 0; }
    static constexpr std::uint32_t WriterTid(Storage w) noexcept {
        return static_cast<std::uint32_t>((w & kWriterTidMask) >> kWriterTidShift);
    }
    static constexpr std::uint32_t Readers(Storage w) noexcept {
        return static_cast<std::uint32_t>(w & kReadersMask);
    }
};
static_assert(sizeof(RwlockWord::Storage) == 8);

// Guest sync slots are 8 bytes (pointer-sized). These asserts tie the word
// size to the SceTypes slot aliases without including SceTypes here.
static_assert(sizeof(std::uint64_t) == sizeof(void*));

}  // namespace SyncWords

#endif
