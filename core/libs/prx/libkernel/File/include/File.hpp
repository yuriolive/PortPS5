// libkernel file API: sceKernelOpen/Read/Write/Lseek/Close/Stat/Unlink/Mkdir/Fsync/Rmdir.
// All exports use APS5_VABI (System V ABI) and return SCE error codes (0x80020000 | FreeBSD errno),
// never exceptions, for ordinary failures. Guest paths go through ResolveKernelPath (sandboxed).
#ifndef CORE_LIBS_PRX_LIBKERNEL_FILE_FILE_HPP
#define CORE_LIBS_PRX_LIBKERNEL_FILE_FILE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// SCE kernel error for a FreeBSD errno: 0x80020000 | errno (e.g. ENOENT -> -2147352574).
constexpr int SceKernelErrno(int freebsdErrno) { return static_cast<int>(0x80020000u | static_cast<unsigned>(freebsdErrno)); }

// Host errno -> SCE kernel error (maps the few values that differ from FreeBSD).
int HostErrnoToSce(int hostErrno);

// Resolves a guest path for the file APIs. Returns 0 and fills `host`, or a positive
// errno: EACCES when a /savedata0 path escapes the container, EFAULT for null,
// ENOENT when /savedata0 is used before any title (param.json) mounted it.
int ResolveKernelPath(const char* path, std::filesystem::path& host);

extern "C" {

/** @brief Opens or creates a file; /savedata0 paths resolve into the per-title save container.
 *  @return fd >= 0, or -(0x80020000|errno): EACCES (escapes /savedata0), ENOENT, EEXIST, EINVAL (bad access mode), EFAULT (null path) */
int APS5_VABI sceKernelOpen(const char* path, int flags, std::uint16_t mode);
/** @brief Closes a descriptor.
 *  @return 0, or SCE error (EBADF) */
int APS5_VABI sceKernelClose(int d);
/** @brief Reads up to nbytes from a descriptor.
 *  @return bytes read (0 at EOF), or SCE error (EBADF, EFAULT for a null buffer, EINVAL) */
std::int64_t APS5_VABI sceKernelRead(int d, void* buf, std::size_t nbytes);
/** @brief Writes nbytes to a descriptor.
 *  @return bytes written, or SCE error (EBADF, EFAULT, ENOSPC, EINVAL) */
std::int64_t APS5_VABI sceKernelWrite(int d, const void* buf, std::size_t nbytes);
/** @brief Repositions the file offset (whence 0=SET, 1=CUR, 2=END).
 *  @return new offset, or SCE error (EINVAL bad whence, EBADF, EOVERFLOW above INT_MAX) */
int APS5_VABI sceKernelLseek(int d, std::int64_t offset, int whence);
/** @brief Fills FileStat for a path.
 *  @return 0, or SCE error (ENOENT, EACCES for a /savedata0 escape); a null path or buffer is an invalid-argument exception */
int APS5_VABI sceKernelStat(const char* path, FileStat* sb);
/** @brief Removes a file.
 *  @return 0, or SCE error (ENOENT, EACCES) */
int APS5_VABI sceKernelUnlink(const char* path);
/** @brief Creates one directory level (POSIX semantics; the parent must exist).
 *  @return 0, or SCE error (EEXIST, ENOENT for a missing parent, EACCES, EFAULT) */
int APS5_VABI sceKernelMkdir(const char* path, std::uint16_t mode);
/** @brief Flushes a descriptor to stable storage (save files rely on it).
 *  @return 0, or SCE error (EBADF, EIO) */
int APS5_VABI sceKernelFsync(int fd);
/** @brief Removes an empty directory.
 *  @return 0, or SCE error (ENOENT, ENOTEMPTY, EACCES) */
int APS5_VABI sceKernelRmdir(const char* path);

}

#endif
