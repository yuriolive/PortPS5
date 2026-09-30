// tests/filesystem/SaveDataMountTests.cpp
// Verifies the per-title /savedata0 mount (docs/spec/save-data.md, PRD F2):
//  - create/write/fsync/read-back round trip through sceKernelOpen/Write/Fsync/Lseek/Read/Close,
//  - sceKernelMkdir inside the container,
//  - sandbox containment: traversal out of /savedata0 returns EACCES, never touches the host,
//  - unmounted /savedata0 fails instead of writing next to the executable,
//  - title id sanitisation (the id is a host path component).
// Mount state is process-global, so every test mounts in SetUp and unmounts in TearDown.
// Not run against game data; uses only a hermetic temp directory.

#include "common/TestHarness.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/File/include/File.hpp"
#include "prx/libkernel/File/include/FileFlags.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>

namespace {

using namespace PortPS5::Testing;

// SCE error for a FreeBSD errno, independent of the (inconsistent) constants in TestHarness.hpp.
constexpr int Sce(int e) { return SceKernelErrno(e); }

class SaveDataMountTest : public TempDirectoryFixture {
protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        ASSERT_TRUE(MountSaveData("PPSA02929", TempDir() / "saves"));
    }
    void TearDown() override {
        UnmountGuestDirectory(SaveDataMountName);
        TempDirectoryFixture::TearDown();
    }
    std::filesystem::path Container() const { return TempDir() / "saves" / "PPSA02929"; }
};

// Invariant: a file created in /savedata0 lands in <saveRoot>/<titleId>/ on the host
// and survives write -> fsync -> close -> reopen -> read with identical bytes.
TEST_F(SaveDataMountTest, CreateWriteFsyncReadBackRoundTrip) {
    const char payload[] = "progress=42;chapter=3";
    const int wfd = sceKernelOpen("/savedata0/slot0.sav",
                                  SCE_KERNEL_O_CREAT | SCE_KERNEL_O_RDWR | SCE_KERNEL_O_TRUNC, 0666);
    ASSERT_GE(wfd, 0);
    EXPECT_EQ(sceKernelWrite(wfd, payload, sizeof(payload)), static_cast<std::int64_t>(sizeof(payload)));
    EXPECT_SCE_OK(sceKernelFsync(wfd));
    EXPECT_SCE_OK(sceKernelClose(wfd));

    EXPECT_TRUE(std::filesystem::exists(Container() / "slot0.sav"));

    const int rfd = sceKernelOpen("/savedata0/slot0.sav", SCE_KERNEL_O_RDONLY, 0);
    ASSERT_GE(rfd, 0);
    char readBack[64] = {};
    EXPECT_EQ(sceKernelRead(rfd, readBack, sizeof(readBack)), static_cast<std::int64_t>(sizeof(payload)));
    EXPECT_STREQ(readBack, payload);
    EXPECT_SCE_OK(sceKernelClose(rfd));
}

// Invariant: lseek positions absolute (SEEK_SET=0), relative (1) and end (2) work and a
// partial overwrite leaves the rest of the file intact.
TEST_F(SaveDataMountTest, LseekOverwriteInPlace) {
    const int fd = sceKernelOpen("/savedata0/seek.bin", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_RDWR, 0666);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(sceKernelWrite(fd, "AAAAAAAA", 8), 8);
    EXPECT_EQ(sceKernelLseek(fd, 2, 0), 2);
    EXPECT_EQ(sceKernelWrite(fd, "BB", 2), 2);
    EXPECT_EQ(sceKernelLseek(fd, 0, 2), 8);
    EXPECT_EQ(sceKernelLseek(fd, 0, 0), 0);
    char buf[9] = {};
    EXPECT_EQ(sceKernelRead(fd, buf, 8), 8);
    EXPECT_STREQ(buf, "AABBAAAA");
    EXPECT_SCE_OK(sceKernelClose(fd));
}

// Invariant: sceKernelMkdir creates nested save directories one level at a time;
// repeating it reports EEXIST and a missing parent reports ENOENT (codes, no abort).
TEST_F(SaveDataMountTest, MkdirInsideContainer) {
    EXPECT_SCE_OK(sceKernelMkdir("/savedata0/profile", 0777));
    EXPECT_TRUE(std::filesystem::is_directory(Container() / "profile"));
    EXPECT_EQ(sceKernelMkdir("/savedata0/profile", 0777), Sce(EEXIST));
    EXPECT_EQ(sceKernelMkdir("/savedata0/missing/child", 0777), Sce(ENOENT));

    const int fd = sceKernelOpen("/savedata0/profile/a.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666);
    ASSERT_GE(fd, 0);
    EXPECT_SCE_OK(sceKernelClose(fd));
}

// Invariant: missing file without O_CREAT returns the ENOENT code, not an exception.
TEST_F(SaveDataMountTest, OpenMissingFileReturnsEnoent) {
    EXPECT_EQ(sceKernelOpen("/savedata0/nope.sav", SCE_KERNEL_O_RDONLY, 0), Sce(ENOENT));
}

// Invariant: any ".." that climbs out of /savedata0 is EACCES and creates nothing
// outside the container, even when it re-enters through another spelling.
TEST_F(SaveDataMountTest, TraversalOutOfContainerIsDenied) {
    const auto outside = TempDir() / "saves" / "escape.bin";
    const char* attacks[] = {
        "/savedata0/../escape.bin",
        "/savedata0/a/../../escape.bin",
        "/savedata0/../PPSA02929/../escape.bin",
        "/savedata0/..\\escape.bin",
        "/savedata0/sub/..\\..\\escape.bin",
        "/app0/../savedata0/../escape.bin",
    };
    for (const char* attack : attacks) {
        EXPECT_EQ(sceKernelOpen(attack, SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(EACCES)) << attack;
        EXPECT_EQ(sceKernelMkdir(attack, 0777), Sce(EACCES)) << attack;
    }
    EXPECT_FALSE(std::filesystem::exists(outside));
    EXPECT_FALSE(std::filesystem::exists(TempDir() / "escape.bin"));
}

// Invariant: ".." that stays inside the container is legal and normalised.
TEST_F(SaveDataMountTest, InternalDotDotStaysInsideContainer) {
    ASSERT_SCE_OK(sceKernelMkdir("/savedata0/d", 0777));
    const int fd = sceKernelOpen("/savedata0/d/../inside.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666);
    ASSERT_GE(fd, 0);
    EXPECT_SCE_OK(sceKernelClose(fd));
    EXPECT_TRUE(std::filesystem::exists(Container() / "inside.sav"));
}

// Invariant: NTFS alternate data stream / drive syntax (':') inside the mount is denied.
TEST_F(SaveDataMountTest, ColonInComponentIsDenied) {
    EXPECT_EQ(sceKernelOpen("/savedata0/file.sav:stream", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(EACCES));
    EXPECT_EQ(sceKernelOpen("/savedata0/C:/x", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(EACCES));
}

// Invariant: the checked resolver maps only /savedata0 to the container; other guest
// paths keep resolving under the guest root (mount does not leak to other names).
TEST_F(SaveDataMountTest, ResolverScopedToMountPoint) {
    const auto inside = ResolveGuestPathChecked("/savedata0/x/y.sav");
    EXPECT_EQ(inside.error, 0);
    EXPECT_EQ(inside.host, std::filesystem::canonical(Container()) / "x" / "y.sav");

    const auto other = ResolveGuestPathChecked("/savedata1/x");
    EXPECT_EQ(other.error, 0);
    EXPECT_FALSE(other.unmounted);
    EXPECT_NE(other.host.parent_path().parent_path(), std::filesystem::canonical(Container()));
}

// Invariant: a null path is EFAULT as a code (guest pointers are untrusted).
TEST_F(SaveDataMountTest, NullPathsReturnEfault) {
    EXPECT_EQ(sceKernelOpen(nullptr, SCE_KERNEL_O_RDONLY, 0), Sce(EFAULT));
    EXPECT_EQ(sceKernelMkdir(nullptr, 0777), Sce(EFAULT));
    EXPECT_EQ(sceKernelRead(0, nullptr, 4), Sce(EFAULT));
    EXPECT_EQ(sceKernelWrite(0, nullptr, 4), Sce(EFAULT));
}

// Invariant: invalid descriptors / whence / access mode are error codes, not exceptions.
TEST_F(SaveDataMountTest, InvalidArgumentsReturnCodes) {
    // A just-closed descriptor is in the CRT's valid range, so the CRT reports EBADF instead of
    // invoking its invalid-parameter handler (which aborts for out-of-range values on UCRT).
    const int fd = sceKernelOpen("/savedata0/closed.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_RDWR, 0666);
    ASSERT_GE(fd, 0);
    ASSERT_SCE_OK(sceKernelClose(fd));
    EXPECT_EQ(sceKernelClose(fd), Sce(EBADF));
    EXPECT_EQ(sceKernelLseek(fd, 0, 0), Sce(EBADF));
    EXPECT_EQ(sceKernelLseek(0, 0, 7), Sce(EINVAL));
    EXPECT_EQ(sceKernelOpen("/savedata0/x", 3 /* invalid access mode */, 0), Sce(EINVAL));
}

// Invariant: with no title mounted, /savedata0 is ENOENT and nothing is created on the
// host (the reserved name must never fall through to <guest root>/savedata0).
TEST(SaveDataUnmounted, ReservedMountFailsWithoutMount) {
    UnmountGuestDirectory(SaveDataMountName);
    const auto stray = ResolvePath_nid_no_patch("/savedata0/stray.sav");
    EXPECT_TRUE(stray.empty());
    EXPECT_EQ(sceKernelOpen("/savedata0/stray.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(ENOENT));
}

// Invariant: the title id is a host path component, so only [A-Za-z0-9_-] (<= 32) is
// accepted; traversal, separators, drive syntax and empty ids never mount.
class SaveDataTitleIdTest : public TempDirectoryFixture {};

TEST_F(SaveDataTitleIdTest, UnsafeTitleIdsAreRejected) {
    const char* bad[] = {"", "..", "../evil", "a/b", "a\\b", "C:", "PPSA 1", "x.y",
                         "0123456789012345678901234567890123"};
    for (const char* id : bad) EXPECT_FALSE(MountSaveData(id, TempDir())) << id;
    EXPECT_TRUE(MountSaveData("PPSA02929", TempDir()));
    EXPECT_TRUE(std::filesystem::is_directory(TempDir() / "PPSA02929"));
    UnmountGuestDirectory(SaveDataMountName);
}

// Invariant: mount names cannot contain separators or ':' (no nested/drive mounts).
TEST_F(SaveDataTitleIdTest, MountNamesRejectSeparators) {
    EXPECT_FALSE(MountGuestDirectory("a/b", TempDir() / "m"));
    EXPECT_FALSE(MountGuestDirectory("a\\b", TempDir() / "m"));
    EXPECT_FALSE(MountGuestDirectory("c:", TempDir() / "m"));
    EXPECT_FALSE(MountGuestDirectory("", TempDir() / "m"));
}

} // namespace
