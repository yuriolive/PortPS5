// tests/kernel/KernelErrorValuesTests.cpp
// Pins the literal SCE kernel error values that the direct-memory API exposes.
// A real console returns 0x80020000 | FreeBSD errno; DirectMemory.hpp used to
// carry private copies in the 0x8001xxxx range (EINVAL 0x80010005), so every
// error from sceKernelAllocateDirectMemory & co. was invisible to a guest that
// compares against the real constant. The assertions use hex literals on
// purpose: comparing against the library's own symbol cannot catch a wrong
// definition. Header-only, so it also runs on non-Windows hosts.

#include "common/TestHarness.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"

namespace {

// Invariant: every code visible through DirectMemory.hpp equals its real value
// (0x80020000 | errno). Fails with the old private 0x8001xxxx constants.
TEST(KernelErrorValues, DirectMemoryHeaderExposesRealSceCodes) {
    EXPECT_EQ(::SCE_KERNEL_ERROR_EINVAL, static_cast<int>(0x80020016u));
    EXPECT_EQ(::SCE_KERNEL_ERROR_EAGAIN, static_cast<int>(0x80020023u));
    EXPECT_EQ(::SCE_KERNEL_ERROR_ENOMEM, static_cast<int>(0x8002000Cu));
    EXPECT_EQ(::SCE_KERNEL_ERROR_EACCES, static_cast<int>(0x8002000Du));
    EXPECT_EQ(::SCE_KERNEL_ERROR_EFAULT, static_cast<int>(0x8002000Eu));
}

// Invariant: the code space is shared with the test harness' independent copy,
// so the two definitions can never drift apart silently.
TEST(KernelErrorValues, MatchesIndependentHarnessCopy) {
    EXPECT_EQ(::SCE_KERNEL_ERROR_EINVAL, PortPS5::Testing::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(::SCE_KERNEL_ERROR_ENOMEM, PortPS5::Testing::SCE_KERNEL_ERROR_ENOMEM);
    EXPECT_EQ(::SCE_KERNEL_ERROR_EFAULT, PortPS5::Testing::SCE_KERNEL_ERROR_EFAULT);
    EXPECT_EQ(::SCE_KERNEL_ERROR_EAGAIN, PortPS5::Testing::SCE_KERNEL_ERROR_EAGAIN);
}

}  // namespace
