// tests/memory/GuestMemoryValidationTests.cpp
// Verifies the guest-pointer range validation API (GuestMemoryValidation.hpp):
// overflow-safe arithmetic, unmapped and protection-mismatch detection,
// ranges that span several registry entries, the host-mapping fallback for
// memory the registry does not track (stacks), and concurrent use while the
// registry is being mutated. All memory is synthetic; nothing needs game data.
//
// Backing memory is mmap/VirtualAlloc private anonymous pages registered in
// GuestAllocations. A tail page is released to create a guaranteed hole just
// past the end of the mapping.

#include "common/TestHarness.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestMemoryValidation.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include <atomic>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

namespace {

using GuestMemoryValidation::CheckReadable;
using GuestMemoryValidation::CheckWritable;
using GuestMemoryValidation::Status;

constexpr std::size_t kPage = 16 * 1024;  // PS5 page; a multiple of every host page size we run on

// Host-protection levels for the unregistered-memory tests.
enum class HostProt { ReadWrite, ReadOnly };

// Maps `pages` host pages followed by one unmapped (guard) page. Returns the
// base; the page at base + pages * kPage is guaranteed not accessible.
void* MapWithHoleAfter(std::size_t pages, HostProt prot = HostProt::ReadWrite) {
#ifdef _WIN32
    auto* base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, (pages + 1) * kPage, MEM_RESERVE | MEM_COMMIT, prot == HostProt::ReadWrite ? PAGE_READWRITE : PAGE_READONLY));
    if (base) VirtualFree(base + pages * kPage, kPage, MEM_DECOMMIT);
    return base;
#else
    void* base = mmap(nullptr, (pages + 1) * kPage, PROT_READ | (prot == HostProt::ReadWrite ? PROT_WRITE : 0), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return nullptr;
    munmap(static_cast<std::uint8_t*>(base) + pages * kPage, kPage);
    return base;
#endif
}

void Unmap(void* base, std::size_t pages) {
#ifdef _WIN32
    VirtualFree(base, 0, MEM_RELEASE);
    (void)pages;
#else
    munmap(base, pages * kPage);
#endif
}

// Registers [base, base+bytes) in GuestAllocations for the lifetime of the object.
class Registered {
public:
    Registered(void* base, std::size_t bytes, bool readable, bool writable) : base_(base) {
        GuestAllocations::Mutation mutation;
        mutation.Add(base, bytes, readable, writable);
    }
    ~Registered() {
        GuestAllocations::Mutation mutation;
        mutation.Remove(base_);
    }
    Registered(const Registered&) = delete;
    Registered& operator=(const Registered&) = delete;

private:
    void* base_;
};

// Invariant: null pointers and meaningless access masks are InvalidRange; a
// zero-length range on a real pointer touches nothing and is Ok.
TEST(GuestMemoryValidation, NullEmptyAndBadAccessMask) {
    int local = 0;
    EXPECT_EQ(CheckReadable(nullptr, 16), Status::InvalidRange);
    EXPECT_EQ(CheckWritable(nullptr, 16), Status::InvalidRange);
    EXPECT_EQ(CheckReadable(nullptr, 0), Status::InvalidRange);
    EXPECT_EQ(CheckReadable(&local, 0), Status::Ok);
    EXPECT_EQ(GuestMemoryValidation::GuestMemoryValidateRange_nid_postfix(&local, 4, 0), Status::InvalidRange);
    EXPECT_EQ(GuestMemoryValidation::GuestMemoryValidateRange_nid_postfix(&local, 4, 0x80), Status::InvalidRange);
}

// Invariant: address + bytes is never computed in a way that can wrap. A range
// that crosses the top of the address space is InvalidRange before any lookup;
// a huge length from a valid pointer is InvalidRange, not an out-of-range read.
TEST(GuestMemoryValidation, OverflowingRangesAreInvalid) {
    int local = 0;
    EXPECT_EQ(CheckReadable(&local, std::numeric_limits<std::size_t>::max()), Status::InvalidRange);
    if constexpr (sizeof(void*) == 8) {
        const auto top = reinterpret_cast<const void*>(std::uintptr_t{0xFFFFFFFFFFFFF000ull});
        EXPECT_EQ(CheckReadable(top, 0x2000), Status::InvalidRange);  // wraps past 2^64
        EXPECT_EQ(CheckReadable(top, 0x1000), Status::InvalidRange);  // ends exactly at 2^64 (== 0 when wrapped)
        EXPECT_EQ(CheckWritable(top, std::numeric_limits<std::size_t>::max()), Status::InvalidRange);
        EXPECT_EQ(CheckReadable(top, 0xFFF), Status::Unmapped);  // fits arithmetically; just not mapped
    }
}

// Invariant: a registered read/write range validates for exactly its bytes;
// the first byte past the end is Unmapped because the next page is a hole.
TEST(GuestMemoryValidation, RegisteredRangeBoundaries) {
    auto* base = static_cast<std::uint8_t*>(MapWithHoleAfter(2));
    ASSERT_NE(base, nullptr);
    {
        Registered range(base, 2 * kPage, true, true);
        EXPECT_EQ(CheckReadable(base, 2 * kPage), Status::Ok);
        EXPECT_EQ(CheckWritable(base, 2 * kPage), Status::Ok);
        EXPECT_EQ(CheckWritable(base + 2 * kPage - 1, 1), Status::Ok);
        EXPECT_EQ(CheckReadable(base + 2 * kPage - 1, 2), Status::Unmapped);  // one byte past the end
        EXPECT_EQ(CheckReadable(base + 2 * kPage, 1), Status::Unmapped);
        EXPECT_EQ(GuestMemoryValidation::GuestMemoryValidateRange_nid_postfix(base, 2 * kPage, GuestMemoryValidation::kRead | GuestMemoryValidation::kWrite), Status::Ok);
    }
    Unmap(base, 2);
}

// Invariant: the registry's guest protection wins over the host protection.
// The host pages are read/write, but the guest mapped them read-only (or
// inaccessible), so writes (or reads) are AccessDenied. This is what keeps a
// tracker-protected page from being misreported as non-writable.
TEST(GuestMemoryValidation, RegisteredProtectionMismatchIsAccessDenied) {
    auto* base = static_cast<std::uint8_t*>(MapWithHoleAfter(2));
    ASSERT_NE(base, nullptr);
    {
        Registered readOnly(base, kPage, true, false);
        Registered noAccess(base + kPage, kPage, false, false);
        EXPECT_EQ(CheckReadable(base, kPage), Status::Ok);
        EXPECT_EQ(CheckWritable(base, kPage), Status::AccessDenied);
        EXPECT_EQ(CheckReadable(base + kPage, 1), Status::AccessDenied);
        EXPECT_EQ(CheckWritable(base + kPage, 1), Status::AccessDenied);
        // A span that starts readable and runs into the no-access page is denied as a whole.
        EXPECT_EQ(CheckReadable(base, 2 * kPage), Status::AccessDenied);
    }
    Unmap(base, 2);
}

// Invariant: a request may span several registry entries (an mprotect splits
// one mapping into pieces). Equal permissions chain; a weaker piece denies the
// span only for the access it lacks.
TEST(GuestMemoryValidation, RangeSpanningSeveralRegistryEntries) {
    auto* base = static_cast<std::uint8_t*>(MapWithHoleAfter(3));
    ASSERT_NE(base, nullptr);
    {
        Registered first(base, kPage, true, true);
        Registered second(base + kPage, kPage, true, true);
        Registered third(base + 2 * kPage, kPage, true, false);
        EXPECT_EQ(CheckWritable(base, 2 * kPage), Status::Ok);        // two adjacent RW entries chain
        EXPECT_EQ(CheckReadable(base, 3 * kPage), Status::Ok);        // read crosses into the RO entry
        EXPECT_EQ(CheckWritable(base, 3 * kPage), Status::AccessDenied);
        EXPECT_EQ(CheckWritable(base + kPage / 2, kPage), Status::Ok);  // straddles entry boundary 1/2
        EXPECT_EQ(CheckWritable(base + 2 * kPage - 1, 2), Status::AccessDenied);  // straddles RW/RO
    }
    Unmap(base, 3);
}

// Invariant: memory the registry does not know is judged by the host mapping,
// so a stack buffer is accepted while an unmapped address is not and a
// host-read-only mapping rejects writes.
TEST(GuestMemoryValidation, UnregisteredMemoryFallsBackToHostMapping) {
    std::uint8_t stackBuffer[256] = {};
    EXPECT_EQ(CheckReadable(stackBuffer, sizeof(stackBuffer)), Status::Ok);
    EXPECT_EQ(CheckWritable(stackBuffer, sizeof(stackBuffer)), Status::Ok);

    auto* rw = static_cast<std::uint8_t*>(MapWithHoleAfter(1));
    auto* ro = static_cast<std::uint8_t*>(MapWithHoleAfter(1, HostProt::ReadOnly));
    ASSERT_NE(rw, nullptr);
    ASSERT_NE(ro, nullptr);
    EXPECT_EQ(CheckWritable(rw, kPage), Status::Ok);
    EXPECT_EQ(CheckReadable(rw + kPage - 4, 8), Status::Unmapped);  // runs into the hole
    EXPECT_EQ(CheckReadable(ro, kPage), Status::Ok);
    EXPECT_EQ(CheckWritable(ro, kPage), Status::AccessDenied);
    Unmap(rw, 1);
    Unmap(ro, 1);
}

// Invariant: a span can alternate registered and unregistered pieces. A host
// mapped but unregistered middle is accepted; if the middle is not mapped the
// span is Unmapped even though both ends are registered.
TEST(GuestMemoryValidation, GapBetweenRegisteredRanges) {
    auto* base = static_cast<std::uint8_t*>(MapWithHoleAfter(3));
    ASSERT_NE(base, nullptr);
    {
        Registered head(base, kPage, true, true);
        Registered tail(base + 2 * kPage, kPage, true, true);
        EXPECT_EQ(CheckWritable(base, 3 * kPage), Status::Ok);  // middle page is host RW, unregistered
    }
    Unmap(base, 3);

    // Same shape, but the middle page is really unmapped.
    auto* sparse = static_cast<std::uint8_t*>(MapWithHoleAfter(3));
    ASSERT_NE(sparse, nullptr);
#ifdef _WIN32
    VirtualFree(sparse + kPage, kPage, MEM_DECOMMIT);
#else
    munmap(sparse + kPage, kPage);
#endif
    {
        Registered head(sparse, kPage, true, true);
        Registered tail(sparse + 2 * kPage, kPage, true, true);
        EXPECT_EQ(CheckReadable(sparse, 3 * kPage), Status::Unmapped);
        EXPECT_EQ(CheckReadable(sparse, kPage), Status::Ok);
        EXPECT_EQ(CheckReadable(sparse + 2 * kPage, kPage), Status::Ok);
    }
#ifdef _WIN32
    VirtualFree(sparse, 0, MEM_RELEASE);
#else
    munmap(sparse, kPage);
    munmap(sparse + 2 * kPage, kPage);
#endif
}

// Invariant: validation is safe while other threads add and remove unrelated
// ranges. A stable registered range must stay Ok for every call. No
// thread_local objects are used (libc's thread-exit hook crashes on
// non-trivial destructors), only atomics.
TEST(GuestMemoryValidation, ConcurrentValidationWhileRegistryChanges) {
    auto* stable = static_cast<std::uint8_t*>(MapWithHoleAfter(1));
    auto* churn = static_cast<std::uint8_t*>(MapWithHoleAfter(1));
    ASSERT_NE(stable, nullptr);
    ASSERT_NE(churn, nullptr);
    {
        Registered range(stable, kPage, true, true);
        std::atomic<bool> stop{false};
        std::atomic<int> failures{0};
        std::vector<std::thread> readers;
        for (int i = 0; i < 4; ++i) {
            readers.emplace_back([&] {
                while (!stop.load(std::memory_order_acquire)) {
                    if (CheckWritable(stable, kPage) != Status::Ok) failures.fetch_add(1);
                    if (CheckReadable(stable + kPage - 1, 2) != Status::Unmapped) failures.fetch_add(1);
                }
            });
        }
        std::thread mutator([&] {
            for (int i = 0; i < 2000; ++i) {
                GuestAllocations::Mutation mutation;
                mutation.Add(churn, kPage, true, true);
                mutation.Remove(churn);
            }
        });
        mutator.join();
        stop.store(true, std::memory_order_release);
        for (auto& t : readers) t.join();
        EXPECT_EQ(failures.load(), 0);
    }
    Unmap(stable, 1);
    Unmap(churn, 1);
}

}  // namespace
