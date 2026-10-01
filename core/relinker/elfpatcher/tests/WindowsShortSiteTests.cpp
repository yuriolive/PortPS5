// GoogleTest suite: PE emission of EXTRQ/INSERTQ register-form sites that the
// converter extended over following instructions (`--to-intel`).
//
// Subsystem: relinker elfpatcher (WindowsTrampolineBuilder) driven by the real
// Amd64OnlyConverter. No CPU execution and no game data: the code segment is a
// synthetic hand-assembled byte string. It checks the end-to-end contract that
// the converter's recorded site length (4 + moved bytes) is honoured by the PE
// patcher: the first 5 bytes become `jmp rel32`, the remainder is NOP padding,
// and the stub returns after the WHOLE moved range rather than after the jump.
#include <codegen/IAmd64OnlyConverter.hpp>
#include <domain/CodeMap.hpp>
#include <elfpatcher/windows/WindowsLoadImage.hpp>
#include <elfpatcher/windows/WindowsTrampolineBuilder.hpp>
#include <io/BufferUtils.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <vector>

namespace {

using namespace Elfpatcher::Windows;
using Bytes = std::vector<std::uint8_t>;

// ELF text lives at file offset 0x200 / address 0x1000; a data segment follows.
std::vector<Domain::ProgramHeader> Headers() {
    return {{1, 5, 0x200, 0x1000, 0, 0x100, 0x100, 0x1000}, {1, 6, 0x300, 0x2000, 0, 0x100, 0x100, 0x1000}};
}

Bytes ElfWith(const Bytes& text) {
    Bytes bytes(0x400, 0);
    // Minimal ELF64 little-endian x86-64 ET_DYN header: WindowsLoadImage validates
    // these fields (class, data, type, machine, program-header table).
    const Bytes ident = {0x7F, 'E', 'L', 'F', 2, 1, 1};
    std::copy(ident.begin(), ident.end(), bytes.begin());
    const auto put = [&](const std::size_t offset, const std::uint64_t value, const std::size_t size) {
        std::memcpy(bytes.data() + offset, &value, size);  // host is little-endian x86-64
    };
    put(16, 3, 2);       // e_type = ET_DYN
    put(18, 62, 2);      // e_machine = EM_X86_64
    put(24, 0x1000, 8);  // e_entry
    put(32, 64, 8);      // e_phoff
    put(54, 56, 2);      // e_phentsize
    put(56, 2, 2);       // e_phnum
    std::fill(bytes.begin() + 0x200, bytes.begin() + 0x300, 0xCC);
    std::copy(text.begin(), text.end(), bytes.begin() + 0x200);
    return bytes;
}

Domain::CodeMap StartsAt(const std::set<std::uint64_t>& offsets) {
    Domain::CodeMap map;
    for (const auto o : offsets)
        map.Starts.insert(0x1000 + o);
    return map;
}

std::int32_t ReadI32(const Bytes& bytes, const std::size_t offset) {
    std::int32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

// Invariant: for a site the converter extended (length 4 + moved bytes), the PE
// text section has `jmp rel32` + NOP padding over exactly that range, and the
// stub's return branch targets site + Length. Parameterised over the moved
// instruction: a 1-byte NOP (site length 5) and a 5-byte `mov eax,1` (length 9).
// Failure mode: a patcher that assumed Length == 5 would return into the middle
// of the moved range, or leave stale bytes of the original instruction after
// the jump.
class WindowsShortSite : public testing::TestWithParam<Bytes> {};

TEST_P(WindowsShortSite, JumpPaddingAndReturnCoverTheWholeMovedRange) {
    const Bytes moved = GetParam();
    Bytes text = {0x66, 0x0F, 0x79, 0xCA};  // EXTRQ xmm1, xmm2
    text.insert(text.end(), moved.begin(), moved.end());
    text.push_back(0xC3);
    const auto source = ElfWith(text);
    const auto headers = Headers();
    const auto converted = Codegen::MakeAmd64OnlyConverter()->Convert(source, {headers[0]}, StartsAt({0, 4, 4 + moved.size()}));
    ASSERT_EQ(converted.Trampolines.size(), 1u);
    const std::size_t length = 4 + moved.size();
    ASSERT_EQ(converted.Trampolines[0].Length, length);

    const WindowsLoadImage image(converted.Bytes, headers);
    auto sections = image.BuildSections();
    auto nextRva = image.GetEndRva();
    WindowsTrampolineBuilder().Build(converted.Trampolines, image, sections, nextRva);
    ASSERT_EQ(sections.back().Name, ".amdstub");

    const auto& code = sections[0].Data;
    ASSERT_EQ(code[0], 0xE9) << "site must start with jmp rel32";
    for (std::size_t i = 5; i < length; ++i)
        EXPECT_EQ(code[i], 0x90) << "padding byte " << i;
    EXPECT_EQ(code[length], 0xC3) << "bytes after the site must be untouched";

    const auto stubRva = static_cast<std::uint32_t>(static_cast<std::int64_t>(LoadRva + 5) + ReadI32(code, 1));
    EXPECT_EQ(stubRva, sections.back().Rva);
    const auto& stub = sections.back().Data;
    const auto branch = converted.Trampolines[0].ReturnBranchOffset;
    ASSERT_EQ(stub[branch], 0xE9);
    const auto returnRva = static_cast<std::int64_t>(stubRva + branch + 5) + ReadI32(stub, branch + 1);
    EXPECT_EQ(returnRva, static_cast<std::int64_t>(LoadRva + length)) << "stub must return after the whole moved range";
}

INSTANTIATE_TEST_SUITE_P(MovedInstructions, WindowsShortSite, testing::Values(
    Bytes{0x90},
    Bytes{0xB8, 0x01, 0x00, 0x00, 0x00}
));

}  // namespace
