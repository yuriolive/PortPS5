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

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPfX = 1, kPfW = 2, kPfR = 4;

struct Ph {
    std::uint32_t type, flags;
    std::uint64_t vaddr, memsz;
    std::uint64_t offset = 0, filesz = 0;
};

// Parse the program-header table the writer appended to the module.
std::vector<Ph> ReadPhdrs(const std::vector<std::uint8_t>& elf) {
    const auto phoff = Io::ReadU64(elf, 32);
    const auto phnum = Io::ReadU16(elf, 56);
    std::vector<Ph> out;
    for (std::size_t i = 0; i < phnum; ++i) {
        const auto o = static_cast<std::size_t>(phoff) + i * 56;
        out.push_back({Io::ReadU32(elf, o), Io::ReadU32(elf, o + 4), Io::ReadU64(elf, o + 16), Io::ReadU64(elf, o + 40),
                       Io::ReadU64(elf, o + 8), Io::ReadU64(elf, o + 32)});
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

// ---- `--to-intel` trampoline sites in guest modules (sce_module) -----------

// Hand-made stub body: 11 NOPs, the 5-byte return-branch placeholder, then
// padding so the body size is a multiple of 16. Its content is irrelevant to
// the writer; only the layout contract (E9 placeholder at ReturnBranchOffset)
// matters.
Codegen::TrampolineSite MakeSite(const std::uint64_t address, const std::size_t length) {
    Codegen::TrampolineSite site;
    site.Offset = address;  // text segment below maps file offset == address
    site.Address = address;
    site.Length = length;
    site.OriginalBytes.assign(length, 0x41);  // not NOP: a re-patch of already patched bytes can never match
    site.Body.assign(16, 0xCC);
    std::fill_n(site.Body.begin(), 11, 0x90);
    site.Body[11] = 0xE9;
    std::fill_n(site.Body.begin() + 12, 4, 0);
    site.ReturnBranchOffset = 11;
    return site;
}

Relinker::GuestImage ImageWithSites(const std::vector<Codegen::TrampolineSite>& sites) {
    auto image = MakeImage({{kPtLoad, kPfR | kPfX, 0, 0, 0, 0x1000, 0x1000, 0x4000}});
    for (const auto& site : sites)
        std::copy(site.OriginalBytes.begin(), site.OriginalBytes.end(), image.Bytes.begin() + static_cast<std::ptrdiff_t>(site.Offset));
    image.Trampolines = sites;
    return image;
}

std::int32_t ReadI32(const std::vector<std::uint8_t>& bytes, const std::uint64_t offset) {
    return static_cast<std::int32_t>(Io::ReadU32(bytes, static_cast<std::size_t>(offset)));
}

// Invariant: every trampoline site becomes `jmp rel32` to its stub, the rest of
// the (possibly extended) site is NOP padding, the stub lies in the appended
// executable block (flags RWX) at 16-byte alignment, and the stub's return
// branch lands on the instruction after the original site (site + Length), not
// after the 5-byte jump. Covers a 5-byte and a 9-byte (extended) site.
// Failure mode: stubs dropped (module keeps AMD-only bytes), a return into the
// middle of the moved range, or a stub in a non-executable segment.
TEST(LinuxGuestModuleWriter, TrampolineSitesJumpToExecutableStubs) {
    const auto image = ImageWithSites({MakeSite(0x100, 5), MakeSite(0x200, 9)});
    const auto out = Elfpatcher::GuestModuleWriter().WriteLinux(image, {}, "$ORIGIN");
    const auto phdrs = ReadPhdrs(out);
    const auto& block = [&]() -> const Ph& {
        for (auto it = phdrs.rbegin(); it != phdrs.rend(); ++it)
            if (it->type == kPtLoad)
                return *it;
        throw std::runtime_error("no PT_LOAD");
    }();
    EXPECT_EQ(block.flags & (kPfR | kPfW | kPfX), kPfR | kPfW | kPfX) << "appended block must be executable";
    for (const std::uint64_t site : {0x100ull, 0x200ull}) {
        const std::size_t length = site == 0x100 ? 5 : 9;
        ASSERT_EQ(out[site], 0xE9) << "site must start with jmp rel32";
        for (std::size_t i = 5; i < length; ++i)
            EXPECT_EQ(out[site + i], 0x90) << "extended site tail must be NOP padding";
        const auto stubAddress = static_cast<std::uint64_t>(static_cast<std::int64_t>(site + 5) + ReadI32(out, site + 1));
        EXPECT_EQ(stubAddress % 16, 0u) << "stub constants need 16-byte alignment";
        ASSERT_GE(stubAddress, block.vaddr);
        ASSERT_LT(stubAddress + 16, block.vaddr + block.memsz);
        const auto stubFile = block.offset + (stubAddress - block.vaddr);
        const auto branch = stubFile + 11;
        ASSERT_EQ(out[branch], 0xE9);
        const auto returnAddress = static_cast<std::uint64_t>(static_cast<std::int64_t>(stubAddress + 11 + 5) + ReadI32(out, branch + 1));
        EXPECT_EQ(returnAddress, site + length) << "stub must return after the whole moved site";
    }
}

// Invariant: malformed or misplaced sites are rejected with RelinkerException
// instead of patching unrelated bytes: a site outside every executable PT_LOAD,
// a site overlapping the appended block, a body whose placeholder is not a
// jmp, and original bytes that do not match the image (stale site list).
// Failure mode: silent corruption of data bytes in a converted module.
TEST(LinuxGuestModuleWriter, MalformedTrampolineSitesAreRejected) {
    const Elfpatcher::GuestModuleWriter writer;
    {  // lies in the RW data segment, not in the executable one
        auto image = MakeImage({
            {kPtLoad, kPfR | kPfX, 0, 0, 0, 0x1000, 0x1000, 0x4000},
            {kPtLoad, kPfR | kPfW, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x4000},
        });
        auto site = MakeSite(0x1100, 5);
        // Original bytes match, so only the executable-segment rule can reject it.
        std::copy(site.OriginalBytes.begin(), site.OriginalBytes.end(), image.Bytes.begin() + 0x1100);
        image.Trampolines = {site};
        EXPECT_THROW(writer.WriteLinux(image, {}, "$ORIGIN"), Domain::RelinkerException);
    }
    {  // placeholder byte is not a jmp
        auto site = MakeSite(0x100, 5);
        site.Body[site.ReturnBranchOffset] = 0x90;
        EXPECT_THROW(writer.WriteLinux(ImageWithSites({site}), {}, "$ORIGIN"), Domain::RelinkerException);
    }
    {  // site shorter than the 5-byte jump
        EXPECT_THROW(writer.WriteLinux(ImageWithSites({MakeSite(0x100, 4)}), {}, "$ORIGIN"), Domain::RelinkerException);
    }
    {  // image bytes no longer match the recorded original bytes
        auto image = ImageWithSites({MakeSite(0x100, 5)});
        image.Bytes[0x102] = 0x42;
        EXPECT_THROW(writer.WriteLinux(image, {}, "$ORIGIN"), Domain::RelinkerException);
    }
    {  // two sites overlapping: the second sees bytes already patched by the first
        EXPECT_THROW(writer.WriteLinux(ImageWithSites({MakeSite(0x100, 9), MakeSite(0x104, 5)}), {}, "$ORIGIN"), Domain::RelinkerException);
    }
    {  // site extends past the original image into the appended block range
        auto site = MakeSite(0x3FFC, 9);
        auto image = ImageWithSites({});
        image.Trampolines = {site};
        EXPECT_THROW(writer.WriteLinux(image, {}, "$ORIGIN"), Domain::RelinkerException);
    }
}

}  // namespace
