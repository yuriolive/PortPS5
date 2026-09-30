// Regression tests for ElfReader header-size and offset-overflow validation.
//
// Subsystem: relinker (core/relinker/relinker/src/parsing/ElfReader.cpp).
// Ported from AnyPS5 a4b13143 (full ELF64 header required) plus an extension:
// untrusted 64-bit file offsets (e_phoff) must not wrap the bounds check.
// All inputs are synthetic bytes built in-test; no game data is involved.
// Threading: none; every test is single-threaded and self-contained.
#include <relinker/parsing/ElfReader.hpp>
#include <relinker/domain/Types.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

// Minimal valid ELF64 identification: 0x7f 'E' 'L' 'F', ELFCLASS64, LSB, v1.
Bytes MakeHeader(std::size_t size) {
    Bytes b(size, 0);
    const std::uint8_t ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
    std::memcpy(b.data(), ident, std::min(sizeof(ident), size));
    return b;
}

template <typename T>
void Put(Bytes& b, std::size_t off, T v) {
    ASSERT_LE(off + sizeof(T), b.size());
    std::memcpy(b.data() + off, &v, sizeof(T));
}

// Invariant: a file shorter than a full Elf64_Ehdr (0x40 bytes) is reported as
// "File too small for ELF header", not as a later out-of-bounds read. Without
// the fix, sizes 20..0x3f passed the guard and failed with the wrong message.
TEST(ElfReaderHeader, TruncatedHeaderReportsTooSmall) {
    for (std::size_t size : {std::size_t{0}, std::size_t{10}, std::size_t{20}, std::size_t{32}, std::size_t{0x3f}}) {
        Relinker::ElfReader reader(MakeHeader(size));
        try {
            reader.ReadHeader();
            FAIL() << "expected RelinkerException for size " << size;
        } catch (const Domain::RelinkerException& e) {
            EXPECT_THAT(e.what(), testing::HasSubstr("File too small for ELF header")) << "size " << size;
        }
    }
}

// Invariant: exactly 0x40 bytes is a complete header and parses.
TEST(ElfReaderHeader, ExactHeaderSizeParses) {
    Relinker::ElfReader reader(MakeHeader(0x40));
    EXPECT_NO_THROW(reader.ReadHeader());
}

// Invariant: a full-size buffer with a bad magic is still rejected as such.
TEST(ElfReaderHeader, BadMagicRejected) {
    Relinker::ElfReader reader(Bytes(0x40, 0));
    try {
        reader.ReadHeader();
        FAIL() << "expected RelinkerException";
    } catch (const Domain::RelinkerException& e) {
        EXPECT_THAT(e.what(), testing::HasSubstr("Invalid ELF magic number"));
    }
}

// Invariant: e_phoff near UINT64_MAX must be rejected as out of bounds. The old
// `offset + N > size` check wrapped to a small value, passed, and memcpy'd from
// far outside the buffer.
TEST(ElfReaderBounds, HugeProgramHeaderOffsetDoesNotWrap) {
    for (std::uint64_t phoff : {~std::uint64_t{0}, ~std::uint64_t{0} - 3, ~std::uint64_t{0} - 7, ~std::uint64_t{0} - 0x30}) {
        Bytes b = MakeHeader(0x40);
        Put<std::uint64_t>(b, 0x20, phoff);   // e_phoff
        Put<std::uint16_t>(b, 0x36, 56);      // e_phentsize
        Put<std::uint16_t>(b, 0x38, 1);       // e_phnum
        Relinker::ElfReader reader(std::move(b));
        EXPECT_THROW(reader.ReadProgramHeaders(), Domain::RelinkerException) << "phoff " << phoff;
    }
}

// Invariant: a well-formed program header table at a valid offset still reads.
TEST(ElfReaderBounds, ValidProgramHeaderStillReads) {
    Bytes b = MakeHeader(0x40 + 56);
    Put<std::uint64_t>(b, 0x20, 0x40);
    Put<std::uint16_t>(b, 0x36, 56);
    Put<std::uint16_t>(b, 0x38, 1);
    Put<std::uint32_t>(b, 0x40, 1);  // PT_LOAD
    Relinker::ElfReader reader(std::move(b));
    const auto headers = reader.ReadProgramHeaders();
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].Type, 1u);
}

}  // namespace
