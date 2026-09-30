#include "prx/libkernel/File/include/FileFlags.hpp"
#include "prx/libkernel/File/include/NativeStat.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/File/include/File.hpp"
#include "prx/libkernel/AppMetadata/include/AppMetadata.hpp"
#include "SceTypes.hpp"

#include <cerrno>
#include <limits>
#include <stdexcept>
#include <string>


#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
static int NativeOpen(const std::filesystem::path& p, int nativeFlags, std::uint16_t mode) {
    return ::_wopen(p.wstring().c_str(), nativeFlags, static_cast<int>(mode));
}
static std::int64_t NativeLseek(int fd, std::int64_t offset, int whence) {
    return ::_lseeki64(fd, offset, whence);
}
static int NativeRead(int fd, void* buf, std::size_t n) {
    if (n > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        errno = EINVAL;
        return -1;
    }
    return ::_read(fd, buf, static_cast<unsigned int>(n));
}
static int NativeWrite(int fd, const void* buf, std::size_t n) {
    if (n > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        errno = EINVAL;
        return -1;
    }
    return ::_write(fd, buf, static_cast<unsigned int>(n));
}
static int NativeClose(int fd) { return ::_close(fd); }
static int NativeUnlink(const std::filesystem::path& p) {
    return ::_wunlink(p.wstring().c_str());
}
// Returns false for an invalid access mode; callers report EINVAL (no throw across the guest ABI).
static bool MapFlags(int sceFlags, int& out) {
    int f = 0;
    const int acc = sceFlags & SCE_KERNEL_O_ACCMODE;
    if (acc == SCE_KERNEL_O_RDONLY) f |= _O_RDONLY;
    else if (acc == SCE_KERNEL_O_WRONLY) f |= _O_WRONLY;
    else if (acc == SCE_KERNEL_O_RDWR) f |= _O_RDWR;
    else return false;
    if (sceFlags & SCE_KERNEL_O_APPEND) f |= _O_APPEND;
    if (sceFlags & SCE_KERNEL_O_CREAT) f |= _O_CREAT;
    if (sceFlags & SCE_KERNEL_O_TRUNC) f |= _O_TRUNC;
    if (sceFlags & SCE_KERNEL_O_EXCL) f |= _O_EXCL;
    f |= _O_BINARY;
    out = f;
    return true;
}
#else
#include <fcntl.h>
#include <unistd.h>
static int NativeOpen(const std::filesystem::path& p, int nativeFlags, std::uint16_t mode) {
    return ::open(p.c_str(), nativeFlags, static_cast<mode_t>(mode));
}
static std::int64_t NativeLseek(int fd, std::int64_t offset, int whence) {
    return ::lseek(fd, static_cast<off_t>(offset), whence);
}
static std::int64_t NativeRead(int fd, void* buf, std::size_t n) {
    return ::read(fd, buf, n);
}
static std::int64_t NativeWrite(int fd, const void* buf, std::size_t n) {
    return ::write(fd, buf, n);
}
static int NativeClose(int fd) { return ::close(fd); }
static int NativeUnlink(const std::filesystem::path& p) {
    return ::unlink(p.c_str());
}
// Returns false for an invalid access mode; callers report EINVAL (no throw across the guest ABI).
static bool MapFlags(int sceFlags, int& out) {
    int f = 0;
    const int acc = sceFlags & SCE_KERNEL_O_ACCMODE;
    if (acc == SCE_KERNEL_O_RDONLY) f |= O_RDONLY;
    else if (acc == SCE_KERNEL_O_WRONLY) f |= O_WRONLY;
    else if (acc == SCE_KERNEL_O_RDWR) f |= O_RDWR;
    else return false;
    if (sceFlags & SCE_KERNEL_O_APPEND) f |= O_APPEND;
    if (sceFlags & SCE_KERNEL_O_CREAT) f |= O_CREAT;
    if (sceFlags & SCE_KERNEL_O_TRUNC) f |= O_TRUNC;
    if (sceFlags & SCE_KERNEL_O_EXCL) f |= O_EXCL;
    if (sceFlags & SCE_KERNEL_O_SYNC) f |= O_SYNC;
    if (sceFlags & SCE_KERNEL_O_DIRECTORY) f |= O_DIRECTORY;
    out = f;
    return true;
}
#endif

// Host errno -> SCE kernel error (0x80020000 | FreeBSD errno). Only errnos below 35
// that every libc shares with FreeBSD (EPERM..EPIPE: ENOENT, EBADF, EACCES, EEXIST,
// EINVAL, ENOSPC ...) pass through. The rest are numbered differently per libc
// (MinGW: EDEADLK 36, ENAMETOOLONG 38, ENOLCK 39, ENOSYS 40, ENOTEMPTY 41; glibc:
// EAGAIN 11, ENAMETOOLONG 36, ENOSYS 38, ELOOP 40), so they are mapped by name.
// EAGAIN/EDEADLK cannot collide as switch labels on any supported libc.
int HostErrnoToSce(int hostErrno) {
    switch (hostErrno) {
    case EAGAIN: hostErrno = 35; break;
    case EDEADLK: hostErrno = 11; break;
    case ENAMETOOLONG: hostErrno = 63; break;
    case ENOLCK: hostErrno = 77; break;
    case ENOSYS: hostErrno = 78; break;
    case ENOTEMPTY: hostErrno = 66; break;
#ifdef ELOOP
    case ELOOP: hostErrno = 62; break;
#endif
    default: break;
    }
    return SceKernelErrno(hostErrno);
}

int ResolveKernelPath(const char* path, std::filesystem::path& host) {
    auto resolved = ResolveGuestPathChecked(path);
    if (resolved.unmounted) {
        // /savedata0 needs the title id, which lives in param.json; loading it
        // mounts the container (AppMetadata.cpp). No title loaded -> no mount.
        try { GetAppTitleId_nid_postfix(); }
        catch (const std::exception& e) { APS5_LOG_ERR("savedata: title load failed: %s", e.what()); }
        catch (...) { APS5_LOG_ERR("savedata: title load failed (unknown exception)%s", ""); }
        resolved = ResolveGuestPathChecked(path);
        if (resolved.unmounted) return ENOENT;
    }
    if (resolved.error != 0) return resolved.error;
    host = std::move(resolved.host);
    return 0;
}

extern "C" {

int APS5_VABI sceKernelOpen(const char* path, int flags, std::uint16_t mode) {
    if (path == nullptr) return SceKernelErrno(EFAULT);
    int nativeFlags = 0;
    if (!MapFlags(flags, nativeFlags)) return SceKernelErrno(EINVAL);
    APS5_LOG_OUT("path=%s flags=0x%X nativeFlags=0x%X mode=0%o", path, flags, nativeFlags, mode);
    std::filesystem::path native;
    if (const int error = ResolveKernelPath(path, native)) return HostErrnoToSce(error);
    int fd = NativeOpen(native, nativeFlags, mode);
    if (fd < 0) return HostErrnoToSce(errno);
    return fd;
}

int APS5_VABI sceKernelClose(int d) {
    if (NativeClose(d) != 0) return HostErrnoToSce(errno);
    return 0;
}

std::int64_t APS5_VABI sceKernelRead(int d, void* buf, std::size_t nbytes) {
    if (buf == nullptr) return SceKernelErrno(EFAULT);
    auto n = NativeRead(d, buf, nbytes);
    if (n < 0) return HostErrnoToSce(errno);
    return static_cast<std::int64_t>(n);
}

std::int64_t APS5_VABI sceKernelWrite(int d, const void* buf, std::size_t nbytes) {
    if (buf == nullptr) return SceKernelErrno(EFAULT);
    auto n = NativeWrite(d, buf, nbytes);
    if (n < 0) return HostErrnoToSce(errno);
    return static_cast<std::int64_t>(n);
}

int APS5_VABI sceKernelLseek(int d, std::int64_t offset, int whence) {
    if (whence < 0 || whence > 2) return SceKernelErrno(EINVAL);
    std::int64_t result = NativeLseek(d, offset, whence);
    if (result < 0) return HostErrnoToSce(errno);
    // The int return type cannot carry offsets >= 2 GiB; report EOVERFLOW (FreeBSD 84).
    if (result > static_cast<std::int64_t>(std::numeric_limits<int>::max())) return SceKernelErrno(84);
    return static_cast<int>(result);
}

int APS5_VABI sceKernelStat(const char* path, FileStat* sb) {
    if (path == nullptr) {
        throw std::invalid_argument(std::string(__func__) + ": path is null");
    }
    if (sb == nullptr) {
        throw std::invalid_argument(std::string(__func__) + ": sb is null");
    }
    std::filesystem::path native;
    if (const int error = ResolveKernelPath(path, native)) return HostErrnoToSce(error);
    // A missing file is ENOENT as a code, not an exception (guests probe with stat).
    if (const int error = File::TryFillFileStat(native, sb)) return HostErrnoToSce(error);
    return 0;
}

int APS5_VABI sceKernelUnlink(const char* path) {
    if (path == nullptr) {
        throw std::invalid_argument(std::string(__func__) + ": path is null");
    }
    std::filesystem::path native;
    if (const int error = ResolveKernelPath(path, native)) return HostErrnoToSce(error);
    if (NativeUnlink(native) != 0) return HostErrnoToSce(errno);
    return 0;
}

}
