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
    // An empty vector's data() may be null, and memcpy with a null pointer is UB
    // even for length 0 (C11 7.24.1p2), so only copy into a non-empty buffer.
    if (!b.empty()) std::memcpy(b.data(), ident, std::min(sizeof(ident), size));
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

// Invariant: ReadSegment/ReadSection reject Offset + Size that wraps past
// UINT64_MAX. Previously Offset=16, FileSize=2^64-8 summed to 8 <= file size,
// passed the check and built a vector from an inverted iterator range.
TEST(ElfReaderBounds, SegmentAndSectionSizeWrapRejected) {
    Relinker::ElfReader reader(MakeHeader(0x40));
    Domain::ProgramHeader ph{};
    ph.Offset = 16;
    ph.FileSize = ~std::uint64_t{0} - 7;
    EXPECT_THROW(reader.ReadSegment(ph), Domain::RelinkerException);
    Domain::SectionHeader sh{};
    sh.Offset = 16;
    sh.SectionSize = ~std::uint64_t{0} - 7;
    EXPECT_THROW(reader.ReadSection(sh), Domain::RelinkerException);
    // In-range reads, including one ending exactly at the end of the file, still work.
    ph.FileSize = 0x30;
    EXPECT_EQ(reader.ReadSegment(ph).size(), 0x30u);
    sh.SectionSize = 0x30;
    EXPECT_EQ(reader.ReadSection(sh).size(), 0x30u);
}

// Invariant: ReadDynamicTags tolerates an absurd p_filesz (wrapping
// Offset + FileSize) by clamping to the file, and stops at DT_NULL.
TEST(ElfReaderBounds, DynamicTagsHugeFileSizeClamped) {
    Bytes b = MakeHeader(0x40 + 32);
    Put<std::int64_t>(b, 0x40, 5);        // DT_STRTAB
    Put<std::uint64_t>(b, 0x48, 0x1234);
    // Second entry is DT_NULL (already zero).
    Relinker::ElfReader reader(std::move(b));
    Domain::ProgramHeader dyn{};
    dyn.Offset = 0x40;
    dyn.FileSize = ~std::uint64_t{0} - 7;
    const auto tags = reader.ReadDynamicTags(dyn);
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0].Value, 0x1234u);
    dyn.Offset = ~std::uint64_t{0} - 3;  // offset itself wraps: no tags, no crash
    EXPECT_TRUE(reader.ReadDynamicTags(dyn).empty());
}

}  // namespace
