// tests/kernel/KernelSleepSignalTests.cpp
// Verification for three small libkernel exports ported from AnyPS5:
//   - sceKernelSleep           (c6d098d4, previously an aborting stub)
//   - sigprocmask/_sigprocmask (c6d098d4, stub removed from the Socket TU;
//                               PortPS5 also turns the invalid-`how` throw into
//                               EINVAL so nothing crosses the APS5_VABI boundary)
// No GPU and no game data needed. (sceKernelLseek, 15b749c2, is deferred: see
// docs/spec/threading.md Open questions 7.)

#include "common/TestHarness.hpp"
#include "SceTypes.hpp"

#include <chrono>
#include <cstdint>

struct GuestSignalSet {
    std::uint32_t bits[4];
};

extern "C" {
unsigned int APS5_VABI sceKernelSleep(unsigned int seconds);
int APS5_VABI _sigprocmask_nid_postfix(int how, const GuestSignalSet* set, GuestSignalSet* previousSet);
int APS5_VABI sigprocmask_nid_postfix(int how, const void* set, void* previousSet);
int* APS5_VABI __error_nid_postfix();
int APS5_VABI usleep_nid_postfix(unsigned int microseconds);
}

namespace {

constexpr int kSigBlock = 1;
constexpr int kSigUnblock = 2;
constexpr int kSigSetmask = 3;

// sceKernelSleep(0) must not abort (it used to be NotImplemented) and reports
// zero unslept seconds; a 1 s sleep actually sleeps about a second.
TEST(KernelSleep, ZeroAndOneSecond) {
    EXPECT_EQ(sceKernelSleep(0), 0u);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(sceKernelSleep(1), 0u);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(900));
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

// Block/unblock/setmask semantics and the previous-mask out parameter, through
// both the syscall-level and the libc-level entry point. The mask is process
// global, so the test restores it to empty.
TEST(KernelSigprocmask, BlockUnblockSetmaskAndPrevious) {
    GuestSignalSet empty{};
    GuestSignalSet previous{};
    ASSERT_EQ(_sigprocmask_nid_postfix(kSigSetmask, &empty, &previous), 0);

    GuestSignalSet two{{1u << 2, 0, 0, 0}};
    GuestSignalSet eleven{{1u << 11, 0, 0, 0}};
    ASSERT_EQ(sigprocmask_nid_postfix(kSigBlock, &two, nullptr), 0);
    ASSERT_EQ(sigprocmask_nid_postfix(kSigBlock, &eleven, &previous), 0);
    EXPECT_EQ(previous.bits[0], 1u << 2);  // mask before the second block.

    // Query only (null set): returns the current mask, changes nothing.
    GuestSignalSet current{};
    ASSERT_EQ(_sigprocmask_nid_postfix(kSigBlock, nullptr, &current), 0);
    EXPECT_EQ(current.bits[0], (1u << 2) | (1u << 11));
    EXPECT_EQ(current.bits[1] | current.bits[2] | current.bits[3], 0u);

    ASSERT_EQ(sigprocmask_nid_postfix(kSigUnblock, &two, nullptr), 0);
    ASSERT_EQ(_sigprocmask_nid_postfix(kSigBlock, nullptr, &current), 0);
    EXPECT_EQ(current.bits[0], 1u << 11);

    ASSERT_EQ(sigprocmask_nid_postfix(kSigSetmask, &two, nullptr), 0);
    ASSERT_EQ(_sigprocmask_nid_postfix(kSigBlock, nullptr, &current), 0);
    EXPECT_EQ(current.bits[0], 1u << 2);

    ASSERT_EQ(sigprocmask_nid_postfix(kSigSetmask, &empty, nullptr), 0);
}

// Regression: an unknown `how` used to throw std::invalid_argument across the
// APS5_VABI boundary. A real kernel returns -1 with errno EINVAL and leaves the
// mask (and the previous-set out parameter) untouched.
TEST(KernelSigprocmask, InvalidHowIsEinvalAndChangesNothing) {
    GuestSignalSet empty{};
    ASSERT_EQ(_sigprocmask_nid_postfix(kSigSetmask, &empty, nullptr), 0);

    GuestSignalSet bit{{1u << 4, 0, 0, 0}};
    GuestSignalSet previous{{0xABCDu, 0, 0, 0}};
    for (int how : {0, 4, -1, 99}) {
        *__error_nid_postfix() = 0;
        EXPECT_EQ(sigprocmask_nid_postfix(how, &bit, &previous), -1) << "how=" << how;
        EXPECT_EQ(*__error_nid_postfix(), 22) << "how=" << how;
        EXPECT_EQ(previous.bits[0], 0xABCDu) << "previous must be untouched, how=" << how;
    }
    GuestSignalSet current{};
    ASSERT_EQ(_sigprocmask_nid_postfix(kSigBlock, nullptr, &current), 0);
    EXPECT_EQ(current.bits[0], 0u);
}

// Invariant: the POSIX usleep export exists (it used to be unresolved), returns 0
// and sleeps at least the requested time; 0 microseconds returns promptly.
TEST(KernelSleep, UsleepSleepsAndReturnsZero) {
    EXPECT_EQ(usleep_nid_postfix(0), 0);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(usleep_nid_postfix(20000), 0);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(19));
}

}  // namespace
