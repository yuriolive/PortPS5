// Regression for configuration ordering in emitted Windows entry stubs.
// Uses synthetic modules and inspects x86-64 call sites; no Windows host or game data.
#include <elfpatcher/windows/WindowsEntryStubBuilder.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

/** Find an instruction sequence; end() means the required call is absent. */
auto Find(const std::vector<std::uint8_t>& bytes, std::initializer_list<std::uint8_t> pattern) {
    return std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end());
}

// The startup call must precede even a guest module's initializer, under eager and
// lazy binding. Its false result must exit, and its true branch must reach init.
TEST(WindowsConfigStartup, RunsBeforeGuestInitializersAndChecksFailure) {
    using namespace Elfpatcher::Windows;
    for (const bool lazy : {false, true}) {
        const auto imports = WindowsImportBuilder{}.Build(0x1000);
        const auto stub = WindowsEntryStubBuilder{}.Build(
            0x2000, 0x1234, imports, {"synthetic.prx"}, {}, "$ORIGIN/libs", lazy, false,
            {{"synthetic.prx", {}, 0x5678, 0}});
        const auto& bytes = stub.Code.Data;
        // call rax; test al,al; jnz: native bool-returning configuration entry.
        const auto config = Find(bytes, {0xff, 0xd0, 0x84, 0xc0, 0x0f, 0x85});
        ASSERT_NE(config, bytes.end());
        // mov ecx,InitRva; add rax,rcx: guest module lifecycle call.
        const auto guest = Find(bytes, {0xb9, 0x78, 0x56, 0, 0, 0x48, 0x01, 0xc8});
        ASSERT_NE(guest, bytes.end());
        ASSERT_LT(config, guest);
        const auto branch = static_cast<std::size_t>(config - bytes.begin()) + 6;
        ASSERT_GE(bytes.size() - branch, 17u);
        std::int32_t displacement;
        std::memcpy(&displacement, bytes.data() + branch, sizeof(displacement));
        const auto success = static_cast<std::int64_t>(branch + 4) + displacement;
        EXPECT_GT(success, static_cast<std::int64_t>(branch + 4));
        EXPECT_LE(success, guest - bytes.begin());
        // False falls through to ExitProcess(1), followed by ud2, never guest init.
        EXPECT_EQ(bytes[branch + 4], 0xb9);
        EXPECT_EQ(bytes[branch + 5], 1);
        EXPECT_EQ(bytes[branch + 9], 0xff);
        EXPECT_EQ(bytes[branch + 10], 0x15);
        std::int32_t exitDisplacement;
        std::memcpy(&exitDisplacement, bytes.data() + branch + 11, sizeof(exitDisplacement));
        EXPECT_EQ(stub.Code.Rva + branch + 15 + exitDisplacement, imports.Functions.at("ExitProcess"));
        EXPECT_EQ(bytes[branch + 15], 0x0f);
        EXPECT_EQ(bytes[branch + 16], 0x0b);
    }
}

} // namespace
