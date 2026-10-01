// core/libs/prx/libSceAgcDriver/tests/BytesEqualTests.cpp
//
// GoogleTest suite for Graphics/BytesEqual.hpp. The TextureCache trusts this helper to decide whether a guest
// texture changed since it was cached; a false "equal" would show stale pixels. Tests are hermetic and need no GPU.
#include "prx/libSceAgcDriver/Graphics/include/BytesEqual.hpp"
#include <cstddef>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {

using AgcDriver::Graphics::BytesEqual;

// Invariant: a single flipped bit at ANY offset (SIMD body and scalar tail, every length around the 64-byte block
// boundary) is detected. Fails if the vector loop skips a lane or the tail is not compared.
TEST(BytesEqual, DetectsSingleBitDifferenceAtEveryOffset) {
    for (std::size_t size = 0; size <= 200; ++size) {
        std::vector<unsigned char> a(size), b(size);
        for (std::size_t i = 0; i < size; ++i) a[i] = b[i] = static_cast<unsigned char>(i * 31u + 7u);
        EXPECT_TRUE(BytesEqual(a.data(), b.data(), size)) << "size " << size;
        for (std::size_t i = 0; i < size; ++i) {
            b[i] ^= 0x10;
            EXPECT_FALSE(BytesEqual(a.data(), b.data(), size)) << "size " << size << " offset " << i;
            b[i] ^= 0x10;
        }
    }
}

// Invariant: the answer matches memcmp for every pair of source misalignments (texture bases are arbitrary guest
// addresses). Precondition: both buffers carry padding so the shifted views stay in bounds.
TEST(BytesEqual, MatchesMemcmpForUnalignedViews) {
    constexpr std::size_t size = 4096 + 13;
    std::vector<unsigned char> a(size + 64), b(size + 64);
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = b[i] = static_cast<unsigned char>(i * 131u);
    for (std::size_t shiftA = 0; shiftA < 16; ++shiftA) {
        for (std::size_t shiftB = 0; shiftB < 16; ++shiftB) {
            const auto* pa = a.data() + shiftA;
            const auto* pb = b.data() + shiftB;
            EXPECT_EQ(BytesEqual(pa, pb, size), std::memcmp(pa, pb, size) == 0);
        }
    }
    // Same underlying bytes at the same shift must be equal even though the two buffers are distinct.
    EXPECT_TRUE(BytesEqual(a.data() + 5, b.data() + 5, size));
}

// Invariant: a difference in the last byte of a large range is found (no early "equal" before the tail), and a
// difference in the first block returns false without reading past it (checked via the result only).
TEST(BytesEqual, DetectsDifferenceInFirstBlockAndLastByteOfLargeRange) {
    std::vector<unsigned char> a(1u << 20, 0xAB), b(1u << 20, 0xAB);
    EXPECT_TRUE(BytesEqual(a.data(), b.data(), a.size()));
    b.back() = 0;
    EXPECT_FALSE(BytesEqual(a.data(), b.data(), a.size()));
    b.back() = 0xAB;
    b.front() = 0;
    EXPECT_FALSE(BytesEqual(a.data(), b.data(), a.size()));
}

}
