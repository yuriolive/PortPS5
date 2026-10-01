// Host stat -> guest FileStat conversion helpers for libkernel.
// Host-only (no guest ABI): called from sceKernelStat/Fstat; thread-safe, no shared state.
#ifndef CORE_LIBS_PRX_LIBKERNEL_FILE_NATIVESTAT_HPP
#define CORE_LIBS_PRX_LIBKERNEL_FILE_NATIVESTAT_HPP

#include <filesystem>
#include "SceTypes.hpp"

namespace File {

void FillFileStat(const std::filesystem::path& nativePath, FileStat* sb);
void FillFileStat(int nativeDescriptor, FileStat* sb);

// Non-throwing stat for guest-facing calls: returns 0, or the host errno (e.g. ENOENT).
int TryFillFileStat(const std::filesystem::path& nativePath, FileStat* sb);

}

#endif
