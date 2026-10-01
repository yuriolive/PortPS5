// tests/modules/PlayGoTests.cpp
// GoogleTest suite for the offline libScePlayGo: chunk-set parsing, the chunk set loaded from a
// synthetic /app0/playgo-chunkdefs.xml at scePlayGoOpen, every error code (values and check order
// follow the KytyPS5 libPlayGo.cpp oracle) and the fixed "everything is local" answers. No game
// data: the XML is generated in a temporary directory aliased as /app0.

#include "SceTypes.hpp"
#include "common/TestHarness.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libScePlayGo/PlayGoInternal.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
int APS5_VABI scePlayGoInitialize(const PlayGoInitParams*) noexcept;
int APS5_VABI scePlayGoTerminate(void) noexcept;
int APS5_VABI scePlayGoOpen(int*, const void*) noexcept;
int APS5_VABI scePlayGoClose(int) noexcept;
int APS5_VABI scePlayGoGetChunkId(int, std::uint16_t*, std::uint32_t, std::uint32_t*) noexcept;
int APS5_VABI scePlayGoGetInstallChunkId(int, std::uint16_t*, std::uint32_t, std::uint32_t*) noexcept;
int APS5_VABI scePlayGoGetEta(int, const std::uint16_t*, std::uint32_t, std::int64_t*) noexcept;
int APS5_VABI scePlayGoGetInstallSpeed(int, std::int32_t*) noexcept;
int APS5_VABI scePlayGoSetInstallSpeed(int, std::int32_t) noexcept;
int APS5_VABI scePlayGoGetLanguageMask(int, std::uint64_t*) noexcept;
int APS5_VABI scePlayGoGetLocus(int, const std::uint16_t*, std::uint32_t, std::int8_t*) noexcept;
int APS5_VABI scePlayGoGetProgress(int, const std::uint16_t*, std::uint32_t, PlayGoProgress*) noexcept;
int APS5_VABI scePlayGoGetToDoList(int, PlayGoToDo*, std::uint32_t, std::uint32_t*) noexcept;
int APS5_VABI scePlayGoSetToDoList(int, const PlayGoToDo*, std::uint32_t) noexcept;
int APS5_VABI scePlayGoPrefetch(int, const std::uint16_t*, std::uint32_t, std::int8_t) noexcept;
int APS5_VABI scePlayGoGetOptionalChunk(int, std::int32_t, PlayGoOptionalChunk*) noexcept;
int APS5_VABI scePlayGoGetSupportedOptionalChunk(int, std::int32_t, PlayGoOptionalChunk*) noexcept;
int APS5_VABI scePlayGoPrefetchOptionalChunk(int, std::int32_t, const PlayGoOptionalChunk*) noexcept;
}

namespace {

using namespace PortPS5::Testing;

constexpr int kUnknown = static_cast<int>(0x80B20001u);
constexpr int kInvalidArgument = static_cast<int>(0x80B20004u);
constexpr int kBadHandle = static_cast<int>(0x80B20009u);
constexpr int kBadPointer = static_cast<int>(0x80B2000Au);
constexpr int kBadSize = static_cast<int>(0x80B2000Bu);
constexpr int kBadChunkId = static_cast<int>(0x80B2000Cu);
constexpr int kBadSpeed = static_cast<int>(0x80B2000Du);
constexpr int kBadLocus = static_cast<int>(0x80B20010u);
constexpr int kBadOptionalType = static_cast<int>(0x80B20024u);

// Parsing: ids come from <chunk id="N"> tags (not <chunks>), default_chunk adds 0..N, 0 is always
// present, ids above 65535 and non-numeric ids are dropped, and junk input yields just {0}.
TEST(PlayGoParse, ExtractsChunkIdsAndDefaultRange) {
    const auto ids = PlayGoParseChunkDefs(
        "<psproject><chunks default_chunk=\"2\"><chunk id=\"7\" label=\"a\"/>"
        "<chunk label=\"b\" id=\"9\"/><chunk id=\"70000\"/><chunk id=\"x\"/></chunks></psproject>");
    EXPECT_EQ(ids, (std::set<std::uint16_t>{0, 1, 2, 7, 9}));
    EXPECT_EQ(PlayGoParseChunkDefs(""), (std::set<std::uint16_t>{0}));
    EXPECT_EQ(PlayGoParseChunkDefs("<chunk id=\"5\""), (std::set<std::uint16_t>{0}));
    EXPECT_EQ(PlayGoParseChunkDefs("<chunk id=\"65535\"/>"), (std::set<std::uint16_t>{0, 65535}));
    // Long digit runs are read whole: leading zeros keep the value, an oversized run is dropped
    // (it used to be truncated after six digits, so "0000012" parsed as chunk 1).
    EXPECT_EQ(PlayGoParseChunkDefs("<chunk id=\"0000012\"/>"), (std::set<std::uint16_t>{0, 12}));
    EXPECT_EQ(PlayGoParseChunkDefs("<chunk id=\"1000000\"/>"), (std::set<std::uint16_t>{0}));
    EXPECT_EQ(PlayGoParseChunkDefs("<chunk id=\"65536\"/>"), (std::set<std::uint16_t>{0}));
}

class PlayGoTest : public TempDirectoryFixture {
protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        AddPathAlias_nid_no_patch("/app0", TempDir().string().c_str());
    }
    void TearDown() override {
        RemovePathAlias_nid_no_patch("/app0");
        TempDirectoryFixture::TearDown();
    }

    /** Writes /app0/playgo-chunkdefs.xml with the given chunk ids. */
    void WriteChunkDefs(const std::vector<int>& ids) {
        std::ofstream out(MakeSubPath("playgo-chunkdefs.xml"));
        out << "<psproject><chunks>";
        for (int id : ids) out << "<chunk id=\"" << id << "\"/>";
        out << "</chunks></psproject>";
    }

    int Open() {
        int handle = 0;
        EXPECT_EQ(scePlayGoOpen(&handle, nullptr), 0);
        return handle;
    }
};

// Initialize needs a block with a buffer; Open needs an out pointer and no parameter block.
TEST_F(PlayGoTest, InitializeAndOpenArgumentErrors) {
    EXPECT_EQ(scePlayGoInitialize(nullptr), kBadPointer);
    PlayGoInitParams params{};
    EXPECT_EQ(scePlayGoInitialize(&params), kBadPointer);
    char buffer[16];
    params.buf_addr = buffer;
    EXPECT_EQ(scePlayGoInitialize(&params), 0);
    EXPECT_EQ(scePlayGoTerminate(), 0);

    int handle = 0;
    EXPECT_EQ(scePlayGoOpen(nullptr, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoOpen(&handle, buffer), kInvalidArgument);
    EXPECT_EQ(scePlayGoOpen(&handle, nullptr), 0);
    EXPECT_EQ(handle, 1);
    EXPECT_EQ(scePlayGoClose(handle), 0);
    EXPECT_EQ(scePlayGoClose(2), kBadHandle);
}

// The chunk set is read from the title's own xml at every open, so a second open sees a changed
// file; with no xml the set is {0}. Listing is ascending and truncated to the capacity.
TEST_F(PlayGoTest, ChunkSetComesFromXmlAtOpen) {
    int handle = Open();
    std::uint16_t ids[8] = {};
    std::uint32_t count = 99;
    ASSERT_EQ(scePlayGoGetChunkId(handle, ids, 8, &count), 0);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(ids[0], 0);

    WriteChunkDefs({5, 3, 9});
    handle = Open();
    ASSERT_EQ(scePlayGoGetChunkId(handle, ids, 8, &count), 0);
    ASSERT_EQ(count, 4u);
    EXPECT_EQ(ids[0], 0);
    EXPECT_EQ(ids[1], 3);
    EXPECT_EQ(ids[2], 5);
    EXPECT_EQ(ids[3], 9);

    ASSERT_EQ(scePlayGoGetChunkId(handle, ids, 2, &count), 0);
    EXPECT_EQ(count, 2u);

    // GetInstallChunkId equals GetChunkId: every chunk is installed (it used to be an abort stub).
    std::uint16_t installed[8] = {};
    std::uint32_t installedCount = 0;
    ASSERT_EQ(scePlayGoGetInstallChunkId(handle, installed, 8, &installedCount), 0);
    EXPECT_EQ(installedCount, 4u);
    EXPECT_EQ(installed[3], 9);

    EXPECT_EQ(scePlayGoGetChunkId(handle, ids, 8, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoGetChunkId(handle, nullptr, 8, &count), kBadPointer);
    EXPECT_EQ(scePlayGoGetChunkId(handle, nullptr, 0, &count), 0);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(scePlayGoGetInstallChunkId(handle, ids, 8, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoGetChunkId(2, ids, 8, &count), kBadHandle);
}

// An overlong path component causes a status-query error rather than confirming absence.
// Failed opens must leave both the caller's handle and the previously loaded chunk set intact.
TEST_F(PlayGoTest, ChunkDefsStatusErrorFailsOpen) {
    WriteChunkDefs({7});
    const int previousHandle = Open();
    const auto app0 = MakeSubPath(std::string(512, 'x'));
    std::error_code ec;
    (void)std::filesystem::exists(app0 / "playgo-chunkdefs.xml", ec);
    // Whether an overlong component is a status error or plain "not found" depends on the OS and
    // file system; only assert the library's behaviour where the fixture really fails the query.
    if (!ec) {
        GTEST_SKIP() << "this platform reports an overlong path as absent, not as a status error";
    }
    AddPathAlias_nid_no_patch("/app0", app0.string().c_str());

    int handle = 99;
    EXPECT_EQ(scePlayGoOpen(&handle, nullptr), kUnknown);
    EXPECT_EQ(handle, 99);
    std::uint16_t ids[2] = {};
    std::uint32_t count = 0;
    ASSERT_EQ(scePlayGoGetChunkId(previousHandle, ids, 2, &count), 0);
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(ids[1], 7);
}

// A directory with the XML's name fails either the open (Windows) or the read (Linux).
// Reading it must return UNKNOWN without terminating at the noexcept guest boundary.
TEST_F(PlayGoTest, UnreadableChunkDefsFailsOpen) {
    WriteChunkDefs({7});
    const int previousHandle = Open();
    ASSERT_TRUE(std::filesystem::remove(MakeSubPath("playgo-chunkdefs.xml")));
    std::filesystem::create_directories(MakeSubPath("playgo-chunkdefs.xml"));
    int handle = 99;
    EXPECT_EQ(scePlayGoOpen(&handle, nullptr), kUnknown);
    EXPECT_EQ(handle, 99);
    std::uint16_t ids[2] = {};
    std::uint32_t count = 0;
    ASSERT_EQ(scePlayGoGetChunkId(previousHandle, ids, 2, &count), 0);
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(ids[1], 7);
}

// Empty files and a final short read are normal EOF; definitions beyond the read buffer survive.
TEST_F(PlayGoTest, EmptyAndLargeChunkDefsReadToEof) {
    std::ofstream(MakeSubPath("playgo-chunkdefs.xml")).close();
    int handle = Open();
    std::uint16_t ids[2] = {};
    std::uint32_t count = 0;
    ASSERT_EQ(scePlayGoGetChunkId(handle, ids, 2, &count), 0);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(ids[0], 0);

    std::ofstream out(MakeSubPath("playgo-chunkdefs.xml"));
    out << std::string(8192, ' ') << "<chunk id=\"7\"/>";
    out.close();
    handle = Open();
    ASSERT_EQ(scePlayGoGetChunkId(handle, ids, 2, &count), 0);
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(ids[1], 7);
}

// Chunk-array calls (Locus, Eta, Progress, Prefetch) share their validation order: handle, pointers,
// size, then membership; valid requests report everything local and complete.
TEST_F(PlayGoTest, ChunkArrayCallsValidateAndReportLocal) {
    WriteChunkDefs({4});
    const int handle = Open();
    const std::uint16_t good[2] = {0, 4};
    const std::uint16_t bad[2] = {0, 5};
    std::int8_t loci[2] = {};
    std::int64_t eta = -1;
    PlayGoProgress progress{};

    EXPECT_EQ(scePlayGoGetLocus(handle, good, 2, loci), 0);
    EXPECT_EQ(loci[0], 3);
    EXPECT_EQ(loci[1], 3);
    EXPECT_EQ(scePlayGoGetEta(handle, good, 2, &eta), 0);
    EXPECT_EQ(eta, 0);
    EXPECT_EQ(scePlayGoGetProgress(handle, good, 2, &progress), 0);
    EXPECT_EQ(progress.progress_size, 2u);
    EXPECT_EQ(progress.total_size, 2u);
    EXPECT_EQ(scePlayGoPrefetch(handle, good, 2, 3), 0);

    EXPECT_EQ(scePlayGoGetLocus(handle, bad, 2, loci), kBadChunkId);
    EXPECT_EQ(scePlayGoGetEta(handle, bad, 2, &eta), kBadChunkId);
    EXPECT_EQ(scePlayGoGetProgress(handle, bad, 2, &progress), kBadChunkId);
    EXPECT_EQ(scePlayGoPrefetch(handle, bad, 2, 3), kBadChunkId);

    EXPECT_EQ(scePlayGoGetLocus(handle, good, 0, loci), kBadSize);
    EXPECT_EQ(scePlayGoGetEta(handle, good, 0, &eta), kBadSize);
    EXPECT_EQ(scePlayGoGetProgress(handle, good, 0, &progress), kBadSize);
    EXPECT_EQ(scePlayGoPrefetch(handle, good, 0, 3), kBadSize);

    EXPECT_EQ(scePlayGoGetLocus(handle, nullptr, 2, loci), kBadPointer);
    EXPECT_EQ(scePlayGoGetLocus(handle, good, 2, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoGetEta(handle, good, 2, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoGetProgress(handle, good, 2, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoPrefetch(handle, nullptr, 2, 3), kBadPointer);

    EXPECT_EQ(scePlayGoPrefetch(handle, good, 2, 1), kBadLocus);
    EXPECT_EQ(scePlayGoPrefetch(handle, good, 2, 0), 0);  // not-downloaded and local-slow are valid minimums.
    EXPECT_EQ(scePlayGoPrefetch(handle, good, 2, 2), 0);

    EXPECT_EQ(scePlayGoGetLocus(2, good, 2, loci), kBadHandle);
    EXPECT_EQ(scePlayGoGetEta(2, good, 2, &eta), kBadHandle);
    EXPECT_EQ(scePlayGoGetProgress(2, good, 2, &progress), kBadHandle);
    EXPECT_EQ(scePlayGoPrefetch(2, good, 2, 3), kBadHandle);
}

// Install speed defaults to FULL (2), accepts 0..2 and rejects anything else without changing it.
TEST_F(PlayGoTest, InstallSpeed) {
    const int handle = Open();
    std::int32_t speed = -1;
    EXPECT_EQ(scePlayGoGetInstallSpeed(handle, nullptr), kBadPointer);
    ASSERT_EQ(scePlayGoSetInstallSpeed(handle, 2), 0);
    ASSERT_EQ(scePlayGoGetInstallSpeed(handle, &speed), 0);
    EXPECT_EQ(speed, 2);
    EXPECT_EQ(scePlayGoSetInstallSpeed(handle, 0), 0);
    ASSERT_EQ(scePlayGoGetInstallSpeed(handle, &speed), 0);
    EXPECT_EQ(speed, 0);
    EXPECT_EQ(scePlayGoSetInstallSpeed(handle, 3), kBadSpeed);
    EXPECT_EQ(scePlayGoSetInstallSpeed(handle, -1), kBadSpeed);
    ASSERT_EQ(scePlayGoGetInstallSpeed(handle, &speed), 0);
    EXPECT_EQ(speed, 0);
    EXPECT_EQ(scePlayGoSetInstallSpeed(2, 1), kBadHandle);
    EXPECT_EQ(scePlayGoGetInstallSpeed(2, &speed), kBadHandle);
}

// The language mask reports every language; optional chunks report all present for language (0)
// and scenario (1) and reject other types; the supported query answers the same.
TEST_F(PlayGoTest, LanguageMaskAndOptionalChunks) {
    const int handle = Open();
    std::uint64_t mask = 0;
    EXPECT_EQ(scePlayGoGetLanguageMask(handle, &mask), 0);
    EXPECT_EQ(mask, ~0ull);
    EXPECT_EQ(scePlayGoGetLanguageMask(handle, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoGetLanguageMask(2, &mask), kBadHandle);

    PlayGoOptionalChunk option{};
    EXPECT_EQ(scePlayGoGetOptionalChunk(handle, 0, &option), 0);
    EXPECT_EQ(option.bitmask, ~0ull);
    EXPECT_EQ(scePlayGoGetOptionalChunk(handle, 1, &option), 0);
    EXPECT_EQ(option.bitmask, 0x1full);
    EXPECT_EQ(scePlayGoGetSupportedOptionalChunk(handle, 1, &option), 0);
    EXPECT_EQ(option.bitmask, 0x1full);
    EXPECT_EQ(scePlayGoGetOptionalChunk(handle, 2, &option), kBadOptionalType);
    EXPECT_EQ(scePlayGoGetOptionalChunk(handle, 0, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoGetOptionalChunk(2, 0, &option), kBadHandle);
    EXPECT_EQ(scePlayGoPrefetchOptionalChunk(handle, 0, &option), 0);
    EXPECT_EQ(scePlayGoPrefetchOptionalChunk(handle, 7, &option), kBadOptionalType);
    EXPECT_EQ(scePlayGoPrefetchOptionalChunk(handle, 0, nullptr), kBadPointer);
}

// The to-do list is always empty; setting one validates chunk ids and loci and changes nothing.
TEST_F(PlayGoTest, ToDoList) {
    WriteChunkDefs({4});
    const int handle = Open();
    PlayGoToDo todo[2] = {};
    std::uint32_t count = 99;
    EXPECT_EQ(scePlayGoGetToDoList(handle, todo, 2, &count), 0);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(scePlayGoGetToDoList(handle, todo, 2, nullptr), kBadPointer);
    EXPECT_EQ(scePlayGoGetToDoList(handle, nullptr, 2, &count), kBadPointer);
    EXPECT_EQ(scePlayGoGetToDoList(2, todo, 2, &count), kBadHandle);

    todo[0] = {0, 3, 0};
    todo[1] = {4, 2, 0};
    EXPECT_EQ(scePlayGoSetToDoList(handle, todo, 2), 0);
    EXPECT_EQ(scePlayGoSetToDoList(handle, nullptr, 2), kBadPointer);
    EXPECT_EQ(scePlayGoSetToDoList(handle, todo, 0), kBadSize);
    todo[1].chunk_id = 5;
    EXPECT_EQ(scePlayGoSetToDoList(handle, todo, 2), kBadChunkId);
    todo[1] = {4, 1, 0};
    EXPECT_EQ(scePlayGoSetToDoList(handle, todo, 2), kBadLocus);
    EXPECT_EQ(scePlayGoSetToDoList(2, todo, 2), kBadHandle);
}

}  // namespace
