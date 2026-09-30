// Regression tests for overflow-safe bounds checks in Io::ReadUxx/WriteUxx and
// Io::ByteReader. Ported from AnyPS5 b6ff4a21 and converted to GoogleTest.
//
// Invariant: `offset + N > size` wraps for offsets near SIZE_MAX and would
// accept an out-of-range access; the fixed form `offset > size || size - offset
// < N` must reject it, without modifying the buffer, while still accepting
// accesses that end exactly at the buffer boundary. Single-threaded.
#include <io/BufferUtils.hpp>
#include <io/ByteReader.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr auto kMax = std::numeric_limits<std::size_t>::max();

// Offsets that are invalid for an access of sizeof(T) into `size` bytes,
// including values whose sum with sizeof(T) wraps past zero.
template <typename T>
std::vector<std::size_t> BadOffsets(std::size_t size) {
    return {size - sizeof(T) + 1, size, size + 1, kMax - sizeof(T), kMax - sizeof(T) + 1, kMax};
}

template <typename T, typename Read>
void CheckRead(Read read) {
    const T value = static_cast<T>(0x8877665544332211ull);
    Bytes bytes(sizeof(T) + 1, 0xCC);
    std::memcpy(bytes.data() + 1, &value, sizeof(T));
    // A read ending exactly at the end of the buffer is valid (unaligned).
    EXPECT_EQ(read(bytes, 1), value);
    for (auto off : BadOffsets<T>(bytes.size())) EXPECT_THROW(read(bytes, off), std::out_of_range) << off;
    for (std::size_t size = 0; size < sizeof(T); ++size) {
        Bytes shortBuf(size, 0xCC);
        EXPECT_THROW(read(shortBuf, 0), std::out_of_range);
        EXPECT_THROW(read(shortBuf, kMax), std::out_of_range);
    }
}

template <typename T, typename Write>
void CheckWrite(Write write) {
    const T value = static_cast<T>(0x8877665544332211ull);
    Bytes bytes(sizeof(T) + 1, 0xCC);
    write(bytes, 1, value);
    Bytes expected(sizeof(T) + 1, 0xCC);
    std::memcpy(expected.data() + 1, &value, sizeof(T));
    EXPECT_EQ(bytes, expected);
    // Failed writes must leave the buffer untouched.
    const Bytes before = bytes;
    for (auto off : BadOffsets<T>(bytes.size())) {
        EXPECT_THROW(write(bytes, off, value), std::out_of_range) << off;
        EXPECT_EQ(bytes, before);
    }
    for (std::size_t size = 0; size < sizeof(T); ++size) {
        Bytes shortBuf(size, 0xCC);
        EXPECT_THROW(write(shortBuf, 0, value), std::out_of_range);
        EXPECT_THROW(write(shortBuf, kMax, value), std::out_of_range);
        EXPECT_EQ(shortBuf, Bytes(size, 0xCC));
    }
}

TEST(BufferBounds, FreeReadFunctions) {
    CheckRead<std::uint16_t>([](const Bytes& b, std::size_t o) { return Io::ReadU16(b, o); });
    CheckRead<std::uint32_t>([](const Bytes& b, std::size_t o) { return Io::ReadU32(b, o); });
    CheckRead<std::uint64_t>([](const Bytes& b, std::size_t o) { return Io::ReadU64(b, o); });
}

TEST(BufferBounds, ByteReaderMethods) {
    const Io::ByteReader r;
    CheckRead<std::uint16_t>([&](const Bytes& b, std::size_t o) { return r.ReadU16(b, o); });
    CheckRead<std::uint32_t>([&](const Bytes& b, std::size_t o) { return r.ReadU32(b, o); });
    CheckRead<std::uint64_t>([&](const Bytes& b, std::size_t o) { return r.ReadU64(b, o); });
}

TEST(BufferBounds, WriteFunctions) {
    CheckWrite<std::uint16_t>([](Bytes& b, std::size_t o, std::uint16_t v) { Io::WriteU16(b, o, v); });
    CheckWrite<std::uint32_t>([](Bytes& b, std::size_t o, std::uint32_t v) { Io::WriteU32(b, o, v); });
    CheckWrite<std::uint64_t>([](Bytes& b, std::size_t o, std::uint64_t v) { Io::WriteU64(b, o, v); });
}

// WriteU8 already used a wrap-free check; keep it covered so it stays that way.
TEST(BufferBounds, WriteU8) {
    Bytes b(2, 0xCC);
    Io::WriteU8(b, 1, 0x11);
    EXPECT_EQ(b[1], 0x11);
    EXPECT_THROW(Io::WriteU8(b, 2, 1), std::out_of_range);
    EXPECT_THROW(Io::WriteU8(b, kMax, 1), std::out_of_range);
}

}  // namespace
