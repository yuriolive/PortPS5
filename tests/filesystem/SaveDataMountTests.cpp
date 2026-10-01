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
#include "prx/libc/include/config/Config.hpp"
#include "prx/libkernel/File/include/File.hpp"
#include "prx/libkernel/File/include/FileFlags.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// POSIX-named exports (Stdio.cpp) under test; not in a public header. APS5_VABI is
// mandatory: the definitions use the System V ABI and a mismatched declaration would
// corrupt argument registers on Windows.
extern "C" {
int APS5_VABI open_nid_postfix(const char* path, int flags, int mode);
int APS5_VABI close_nid_postfix(int d);
std::int64_t APS5_VABI read_nid_postfix(int d, void* buf, std::uint64_t nbytes);
std::int64_t APS5_VABI write_nid_postfix(int d, const char* str, std::int64_t size);
std::int64_t APS5_VABI lseek_nid_postfix(int d, std::int64_t offset, int whence);
int APS5_VABI stat_nid_postfix(const char* path, FileStat* sb);
int APS5_VABI unlink_nid_postfix(const char* path);
int APS5_VABI rmdir_nid_postfix(const char* path);
int APS5_VABI mkdir_nid_postfix(const char* path, std::uint16_t mode);
}

namespace {

using namespace PortPS5::Testing;

// SCE error for a FreeBSD errno, independent of the (inconsistent) constants in TestHarness.hpp.
constexpr int Sce(int e) { return SceKernelErrno(e); }

/**
 * Fixture: mounts a fresh per-title container (PPSA02929) under a hermetic temp dir in SetUp and
 * unmounts it in TearDown, because the mount table is process-global.
 */
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

/**
 * Invariant: a file created in /savedata0 lands in <saveRoot>/<titleId>/ on the host
 * and survives write -> fsync -> close -> reopen -> read with identical bytes.
 */
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

/**
 * Invariant: lseek positions absolute (SEEK_SET=0), relative (1) and end (2) work and a
 * partial overwrite leaves the rest of the file intact.
 */
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

/**
 * Invariant: sceKernelMkdir creates nested save directories one level at a time;
 * repeating it reports EEXIST and a missing parent reports ENOENT (codes, no abort).
 */
TEST_F(SaveDataMountTest, MkdirInsideContainer) {
    EXPECT_SCE_OK(sceKernelMkdir("/savedata0/profile", 0777));
    EXPECT_TRUE(std::filesystem::is_directory(Container() / "profile"));
    EXPECT_EQ(sceKernelMkdir("/savedata0/profile", 0777), Sce(EEXIST));
    EXPECT_EQ(sceKernelMkdir("/savedata0/missing/child", 0777), Sce(ENOENT));

    const int fd = sceKernelOpen("/savedata0/profile/a.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666);
    ASSERT_GE(fd, 0);
    EXPECT_SCE_OK(sceKernelClose(fd));
}

/**
 * Invariant: missing file without O_CREAT returns the ENOENT code, not an exception.
 */
TEST_F(SaveDataMountTest, OpenMissingFileReturnsEnoent) {
    EXPECT_EQ(sceKernelOpen("/savedata0/nope.sav", SCE_KERNEL_O_RDONLY, 0), Sce(ENOENT));
}

/**
 * Invariant: any ".." that climbs out of /savedata0 is EACCES and creates nothing
 * outside the container, even when it re-enters through another spelling.
 */
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

/**
 * Invariant: ".." that stays inside the container is legal and normalised.
 */
TEST_F(SaveDataMountTest, InternalDotDotStaysInsideContainer) {
    ASSERT_SCE_OK(sceKernelMkdir("/savedata0/d", 0777));
    const int fd = sceKernelOpen("/savedata0/d/../inside.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666);
    ASSERT_GE(fd, 0);
    EXPECT_SCE_OK(sceKernelClose(fd));
    EXPECT_TRUE(std::filesystem::exists(Container() / "inside.sav"));
}

/**
 * Invariant: NTFS alternate data stream / drive syntax (':') inside the mount is denied.
 */
TEST_F(SaveDataMountTest, ColonInComponentIsDenied) {
    EXPECT_EQ(sceKernelOpen("/savedata0/file.sav:stream", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(EACCES));
    EXPECT_EQ(sceKernelOpen("/savedata0/C:/x", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(EACCES));
}

/**
 * Invariant: the checked resolver maps only /savedata0 to the container; other guest
 * paths keep resolving under the guest root (mount does not leak to other names).
 */
TEST_F(SaveDataMountTest, ResolverScopedToMountPoint) {
    const auto inside = ResolveGuestPathChecked("/savedata0/x/y.sav");
    EXPECT_EQ(inside.error, 0);
    EXPECT_EQ(inside.host, std::filesystem::canonical(Container()) / "x" / "y.sav");

    const auto other = ResolveGuestPathChecked("/savedata1/x");
    EXPECT_EQ(other.error, 0);
    EXPECT_FALSE(other.unmounted);
    EXPECT_NE(other.host.parent_path().parent_path(), std::filesystem::canonical(Container()));
}

/**
 * Invariant: a null path is EFAULT as a code (guest pointers are untrusted).
 */
TEST_F(SaveDataMountTest, NullPathsReturnEfault) {
    EXPECT_EQ(sceKernelOpen(nullptr, SCE_KERNEL_O_RDONLY, 0), Sce(EFAULT));
    EXPECT_EQ(sceKernelMkdir(nullptr, 0777), Sce(EFAULT));
    EXPECT_EQ(sceKernelRead(0, nullptr, 4), Sce(EFAULT));
    EXPECT_EQ(sceKernelWrite(0, nullptr, 4), Sce(EFAULT));
}

/**
 * Invariant: invalid descriptors / whence / access mode are error codes, not exceptions.
 */
TEST_F(SaveDataMountTest, InvalidArgumentsReturnCodes) {
#ifdef _WIN32
    // UCRT calls its invalid-parameter handler for a bad descriptor and the default handler
    // terminates the process, so the test must not depend on which fd values the CRT treats
    // as "merely closed". Install a returning handler (MinGW exposes only the process-wide setter; the test is single-threaded here); the CRT then takes its
    // documented path (return -1, errno = EBADF). Restored on scope exit.
    struct InvalidParameterGuard {
        _invalid_parameter_handler previous;
        static void Ignore(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {}
        InvalidParameterGuard() : previous(_set_invalid_parameter_handler(&Ignore)) {}
        ~InvalidParameterGuard() { _set_invalid_parameter_handler(previous); }
    } invalidParameterGuard;
#endif
    EXPECT_EQ(sceKernelClose(-1), Sce(EBADF));
    EXPECT_EQ(sceKernelClose(99999), Sce(EBADF));
    EXPECT_EQ(sceKernelLseek(99999, 0, 0), Sce(EBADF));
    const int fd = sceKernelOpen("/savedata0/closed.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_RDWR, 0666);
    ASSERT_GE(fd, 0);
    ASSERT_SCE_OK(sceKernelClose(fd));
    EXPECT_EQ(sceKernelClose(fd), Sce(EBADF));
    EXPECT_EQ(sceKernelLseek(fd, 0, 0), Sce(EBADF));
    EXPECT_EQ(sceKernelLseek(0, 0, 7), Sce(EINVAL));
    EXPECT_EQ(sceKernelOpen("/savedata0/x", 3 /* invalid access mode */, 0), Sce(EINVAL));
}

/**
 * Invariant: with no title mounted, /savedata0 is ENOENT and nothing is created on the
 * host (the reserved name must never fall through to <guest root>/savedata0).
 */
TEST(SaveDataUnmounted, ReservedMountFailsWithoutMount) {
    UnmountGuestDirectory(SaveDataMountName);
    const auto stray = ResolvePath_nid_no_patch("/savedata0/stray.sav");
    EXPECT_TRUE(stray.empty());
    EXPECT_EQ(sceKernelOpen("/savedata0/stray.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(ENOENT));
}

/**
 * Invariant: the title id is a host path component, so only [A-Za-z0-9_-] (<= 32) is
 * accepted; traversal, separators, drive syntax and empty ids never mount.
 */
class SaveDataTitleIdTest : public TempDirectoryFixture {};

/**
 * Invariant: titleId is a host path component; empty, dotted, separator, drive and over-long ids never mount.
 */
TEST_F(SaveDataTitleIdTest, UnsafeTitleIdsAreRejected) {
    const char* bad[] = {"", "..", "../evil", "a/b", "a\\b", "C:", "PPSA 1", "x.y",
                         "0123456789012345678901234567890123"};
    for (const char* id : bad) EXPECT_FALSE(MountSaveData(id, TempDir())) << id;
    EXPECT_TRUE(MountSaveData("PPSA02929", TempDir()));
    EXPECT_TRUE(std::filesystem::is_directory(TempDir() / "PPSA02929"));
    UnmountGuestDirectory(SaveDataMountName);
}

/**
 * Invariant: mount names cannot contain separators or ':' (no nested/drive mounts).
 */
TEST_F(SaveDataTitleIdTest, MountNamesRejectSeparators) {
    EXPECT_FALSE(MountGuestDirectory("a/b", TempDir() / "m"));
    EXPECT_FALSE(MountGuestDirectory("a\\b", TempDir() / "m"));
    EXPECT_FALSE(MountGuestDirectory("c:", TempDir() / "m"));
    EXPECT_FALSE(MountGuestDirectory("", TempDir() / "m"));
}

/**
 * Invariant (review: errno table): host errnos whose numbers differ between MinGW/glibc and
 * FreeBSD are mapped by name, so a long save path reports ENAMETOOLONG (63), not ENOTSOCK (38).
 */
TEST(SaveDataErrno, HostErrnosMapToFreeBsdValues) {
    EXPECT_EQ(HostErrnoToSce(ENAMETOOLONG), Sce(63));
    EXPECT_EQ(HostErrnoToSce(ENOTEMPTY), Sce(66));
    EXPECT_EQ(HostErrnoToSce(ENOSYS), Sce(78));
    EXPECT_EQ(HostErrnoToSce(EAGAIN), Sce(35));
    EXPECT_EQ(HostErrnoToSce(ENOENT), Sce(2));
    EXPECT_EQ(HostErrnoToSce(EEXIST), Sce(17));
    EXPECT_EQ(HostErrnoToSce(ENOSPC), Sce(28));
}

/**
 * Invariant (review: rmdir/chmod used the unchecked resolver): rmdir returns codes for
 * escapes, unmounted and missing dirs, and removes an empty container directory.
 */
TEST_F(SaveDataMountTest, RmdirUsesCheckedResolver) {
    ASSERT_SCE_OK(sceKernelMkdir("/savedata0/gone", 0777));
    EXPECT_SCE_OK(sceKernelRmdir("/savedata0/gone"));
    EXPECT_FALSE(std::filesystem::exists(Container() / "gone"));
    EXPECT_EQ(sceKernelRmdir("/savedata0/gone"), Sce(ENOENT));
    EXPECT_EQ(sceKernelRmdir("/savedata0/../x"), Sce(EACCES));
}

/**
 * Invariant: rmdir on the reserved /savedata0 name with no title mounted is ENOENT, not a fall-through to the cwd.
 */
TEST(SaveDataUnmounted, RmdirUnmountedReturnsEnoent) {
    UnmountGuestDirectory(SaveDataMountName);
    EXPECT_EQ(sceKernelRmdir("/savedata0/x"), Sce(ENOENT));
}

/**
 * Invariant (review: symlink/junction): a link inside the container that points outside is
 * EACCES for both existing and new children. Skipped where links cannot be created
 * (Windows without Developer Mode / privilege).
 */
TEST_F(SaveDataMountTest, SymlinkOutOfContainerIsDenied) {
    const auto outside = TempDir() / "outside";
    std::filesystem::create_directories(outside);
    std::error_code ec;
#ifdef _WIN32
    // Windows: an NTFS junction (reparse point) needs no privilege and exercises the same
    // escape. The libstdc++ symlink API is avoided on purpose: it needs a privilege locally
    // and the CI run of this test hung when that API succeeded on the elevated runner.
    const std::string cmd = "cmd /c mklink /J \"" + (Container() / "link").string() + "\" \"" + outside.string() + "\" >nul 2>nul";
    if (std::system(cmd.c_str()) != 0) ec = std::make_error_code(std::errc::operation_not_permitted);
#else
    std::filesystem::create_directory_symlink(outside, Container() / "link", ec);
#endif
    if (ec) GTEST_SKIP() << "cannot create directory symlink: " << ec.message();
    // Unlink before TearDown. remove_all() over a link that leaves the tree hung the hosted
    // runner (ctest ran 30+ min with no output); removing the link itself never follows it.
    struct LinkRemover {
        std::filesystem::path link;
        ~LinkRemover() {
#ifdef _WIN32
            RemoveDirectoryW(link.c_str());  // deletes the junction, not its target
#else
            std::error_code ignored;
            std::filesystem::remove(link, ignored);
#endif
        }
    } linkRemover{Container() / "link"};
    EXPECT_EQ(sceKernelOpen("/savedata0/link/leak.bin", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_WRONLY, 0666), Sce(EACCES));
    EXPECT_EQ(sceKernelMkdir("/savedata0/link/sub", 0777), Sce(EACCES));
    EXPECT_FALSE(std::filesystem::exists(outside / "leak.bin"));
}

/**
 * Invariant (sibling-project edge cases): duplicate and trailing slashes normalise, a longer
 * name sharing the mount prefix is not the mount, and backslash traversal is caught.
 */
TEST_F(SaveDataMountTest, SlashAndPrefixEdgeCases) {
    const auto plain = ResolveGuestPathChecked("/savedata0/a/b.sav");
    EXPECT_EQ(ResolveGuestPathChecked("/savedata0//a///b.sav").host, plain.host);
    EXPECT_EQ(ResolveGuestPathChecked("/savedata0/a/b.sav/").host, plain.host);
    EXPECT_FALSE(ResolveGuestPathChecked("/savedata00/x").unmounted);
    EXPECT_NE(ResolveGuestPathChecked("/savedata00/x").host.parent_path().parent_path(), plain.host.parent_path().parent_path());
    EXPECT_EQ(ResolveGuestPathChecked("/savedata0\\..\\x").error, 13);
}

/**
 * Invariant (review: POSIX wrappers leaked SCE codes): the POSIX-named exports return -1 and
 * set errno (FreeBSD value), while sceKernel* return the SCE code.
 */
TEST_F(SaveDataMountTest, PosixWrappersReturnMinusOneAndErrno) {
    errno = 0;
    EXPECT_EQ(open_nid_postfix("/savedata0/none.sav", SCE_KERNEL_O_RDONLY, 0), -1);
    EXPECT_EQ(errno, ENOENT);
    errno = 0;
    EXPECT_EQ(open_nid_postfix("/savedata0/../x", SCE_KERNEL_O_RDONLY, 0), -1);
    EXPECT_EQ(errno, EACCES);
    errno = 0;
    FileStat st{};
    EXPECT_EQ(stat_nid_postfix("/savedata0/none.sav", &st), -1);
    EXPECT_EQ(errno, ENOENT);
    errno = 0;
    EXPECT_EQ(unlink_nid_postfix("/savedata0/none.sav"), -1);
    EXPECT_EQ(errno, ENOENT);
    errno = 0;
    EXPECT_EQ(rmdir_nid_postfix("/savedata0/none"), -1);
    EXPECT_EQ(errno, ENOENT);
    errno = 0;
    EXPECT_EQ(read_nid_postfix(0, nullptr, 4), -1);
    EXPECT_EQ(errno, EFAULT);

    const int fd = open_nid_postfix("/savedata0/p.sav", SCE_KERNEL_O_CREAT | SCE_KERNEL_O_RDWR, 0666);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(write_nid_postfix(fd, "abc", 3), 3);
    EXPECT_EQ(lseek_nid_postfix(fd, 0, 0), 0);
    char buf[4] = {};
    EXPECT_EQ(read_nid_postfix(fd, buf, 3), 3);
    EXPECT_EQ(close_nid_postfix(fd), 0);
    EXPECT_EQ(unlink_nid_postfix("/savedata0/p.sav"), 0);

    // Review (mkdir threw on a host failure): an existing directory, a missing parent, an
    // escape and a null path are all -1 + errno, never an exception across the guest ABI.
    EXPECT_EQ(mkdir_nid_postfix("/savedata0/m", 0777), 0);
    errno = 0;
    EXPECT_EQ(mkdir_nid_postfix("/savedata0/m", 0777), -1);
    EXPECT_EQ(errno, EEXIST);
    errno = 0;
    EXPECT_EQ(mkdir_nid_postfix("/savedata0/nope/child", 0777), -1);
    EXPECT_EQ(errno, ENOENT);
    errno = 0;
    EXPECT_EQ(mkdir_nid_postfix("/savedata0/../m2", 0777), -1);
    EXPECT_EQ(errno, EACCES);
    errno = 0;
    EXPECT_EQ(mkdir_nid_postfix(nullptr, 0777), -1);
    EXPECT_EQ(errno, EFAULT);
}

/**
 * Invariant (CI policy: no getenv outside Config): the default save root is discovered through
 * Config::HostEnvironmentValue and still honours the platform variable, with empty = unset.
 */
TEST(SaveDataDefaultRoot, UsesHostEnvironmentThroughConfig) {
#ifdef _WIN32
    const char* name = "LOCALAPPDATA";
    const auto previous = PortPS5::Config::HostEnvironmentValue(name);
    _putenv_s(name, "C:/portps5_env_probe");
    EXPECT_EQ(DefaultSaveDataRoot(), std::filesystem::path("C:/portps5_env_probe") / "PortPS5" / "saves");
    _putenv_s(name, "");
    EXPECT_FALSE(PortPS5::Config::HostEnvironmentValue(name).has_value());
    if (previous) _putenv_s(name, previous->c_str());
#else
    const char* name = "XDG_DATA_HOME";
    const auto previous = PortPS5::Config::HostEnvironmentValue(name);
    setenv(name, "/tmp/portps5_env_probe", 1);
    EXPECT_EQ(DefaultSaveDataRoot(), std::filesystem::path("/tmp/portps5_env_probe") / "PortPS5" / "saves");
    setenv(name, "", 1);
    EXPECT_FALSE(PortPS5::Config::HostEnvironmentValue(name).has_value());
    if (previous) setenv(name, previous->c_str(), 1); else unsetenv(name);
#endif
}

} // namespace
