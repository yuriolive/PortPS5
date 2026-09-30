// Regression tests for GuestModuleWriter::WriteLinux PT_LOAD handling.
// Ported from AnyPS5 346b7280.
//
// Invariant: a PT_LOAD with no permission flags (the SCE dynlib data segment,
// never mapped on the console) must be dropped from the emitted program
// headers. Forcing PF_R onto it maps it read-only and can shadow the bss tail
// of the preceding RW segment, faulting the module initializer on its first
// write. Flagged PT_LOADs keep PF_R added. The appended block must still be
// placed beyond the dropped segment's address range. Synthetic image only.
#include <elfpatcher/general/GuestModuleWriter.hpp>
#include <io/BufferUtils.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPfX = 1, kPfW = 2, kPfR = 4;

struct Ph {
    std::uint32_t type, flags;
    std::uint64_t vaddr, memsz;
};

// Parse the program-header table the writer appended to the module.
std::vector<Ph> ReadPhdrs(const std::vector<std::uint8_t>& elf) {
    const auto phoff = Io::ReadU64(elf, 32);
    const auto phnum = Io::ReadU16(elf, 56);
    std::vector<Ph> out;
    for (std::size_t i = 0; i < phnum; ++i) {
        const auto o = static_cast<std::size_t>(phoff) + i * 56;
        out.push_back({Io::ReadU32(elf, o), Io::ReadU32(elf, o + 4), Io::ReadU64(elf, o + 16), Io::ReadU64(elf, o + 40)});
    }
    return out;
}

Relinker::GuestImage MakeImage(std::vector<Domain::ProgramHeader> headers) {
    Relinker::GuestImage image;
    image.OutputName = "t.prx";
    image.Bytes.assign(0x4000, 0);
    image.Bytes[0] = 0x7f;
    image.Headers = std::move(headers);
    image.Symbols.push_back({"", 0, 0, 0, 0, 0});  // index 0 is the null symbol
    image.Dynamic.DynSymData.assign(24, 0);
    image.Dynamic.DynStrData.assign(1, 0);
    image.UsePlatformTlsResolver = false;
    return image;
}

TEST(LinuxGuestModuleWriter, FlaglessLoadSegmentIsDropped) {
    // Text (R+X), RW data, and a flagless segment starting inside the RW page.
    auto image = MakeImage({
        {kPtLoad, kPfR | kPfX, 0, 0, 0, 0x1000, 0x1000, 0x4000},
        {kPtLoad, kPfR | kPfW, 0x1000, 0x1000, 0x1000, 0x800, 0x1800, 0x4000},
        {kPtLoad, 0, 0x2800, 0x2800, 0x2800, 0x100, 0x100, 0x4000},
    });
    const auto out = Elfpatcher::GuestModuleWriter().WriteLinux(image, {}, "$ORIGIN");
    std::vector<Ph> loads;
    for (const auto& p : ReadPhdrs(out))
        if (p.type == kPtLoad) loads.push_back(p);
    // text + data + appended block; the flagless segment is gone.
    ASSERT_EQ(loads.size(), 3u);
    for (const auto& p : loads) EXPECT_NE(p.flags, 0u) << "no PT_LOAD may be left flagless";
    EXPECT_EQ(loads[0].flags, kPfR | kPfX);
    EXPECT_EQ(loads[1].flags, kPfR | kPfW);
    // Appended block starts past the dropped segment's end (0x2900), 0x4000-aligned.
    EXPECT_GE(loads[2].vaddr, 0x2900u);
    EXPECT_EQ(loads[2].vaddr % 0x4000, 0u);
}

// Invariant: a write-only segment still gets read permission added.
TEST(LinuxGuestModuleWriter, FlaggedLoadGetsReadPermission) {
    auto image = MakeImage({{kPtLoad, kPfW, 0, 0, 0, 0x1000, 0x1000, 0x4000}});
    const auto out = Elfpatcher::GuestModuleWriter().WriteLinux(image, {}, "$ORIGIN");
    const auto phdrs = ReadPhdrs(out);
    ASSERT_FALSE(phdrs.empty());
    EXPECT_EQ(phdrs[0].type, kPtLoad);
    EXPECT_EQ(phdrs[0].flags, kPfW | kPfR);
}

}  // namespace
