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

int APS5_VABI sceKernelOpen(const char* path, int flags, std::uint16_t mode);
int APS5_VABI sceKernelClose(int d);
std::int64_t APS5_VABI sceKernelRead(int d, void* buf, std::size_t nbytes);
std::int64_t APS5_VABI sceKernelWrite(int d, const void* buf, std::size_t nbytes);
int APS5_VABI sceKernelLseek(int d, std::int64_t offset, int whence);
int APS5_VABI sceKernelStat(const char* path, FileStat* sb);
int APS5_VABI sceKernelUnlink(const char* path);
int APS5_VABI sceKernelMkdir(const char* path, std::uint16_t mode);
int APS5_VABI sceKernelFsync(int fd);

}

#endif
