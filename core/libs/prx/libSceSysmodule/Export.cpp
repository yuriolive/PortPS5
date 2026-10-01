// core/libs/prx/libSceSysmodule/Export.cpp
// libSceSysmodule: bookkeeping of "loaded" system modules by numeric id. Nothing is loaded for real:
// the PRX replacements are linked by the relinker, so Load/Unload only keep a reference count per id
// (under one mutex) and answer IsLoaded from it. Ids missing from kModuleTable are accepted and
// logged so a title that probes newer modules keeps running. Exports are APS5_VABI.

#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#endif

#include "SceTypes.hpp"
#include "ModuleTable.hpp"
#include "prx/libc/include/General.hpp"

namespace {

const char* findModuleName(const std::uint32_t id) {
    const auto it = kModuleTable.find(id);
    return it != kModuleTable.end() ? it->second : nullptr;
}

std::mutex gMutex;
std::unordered_map<std::uint32_t, std::int32_t> gLoadCount;

bool fillModuleInfoForUnwind(std::uint64_t addr, ModuleInfoForUnwind* info) {
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
        return false;
    }
    info->st_size = sizeof(ModuleInfoForUnwind);
    info->eh_frame_hdr_addr = 0;
    info->eh_frame_addr = 0;
    info->eh_frame_size = 0;
    info->seg0_addr = reinterpret_cast<std::uint64_t>(mbi.BaseAddress);
    info->seg0_size = mbi.RegionSize;
    char path[4096] = {};
    DWORD len = GetMappedFileNameA(GetCurrentProcess(), mbi.BaseAddress, path, sizeof(path) - 1);
    path[len] = '\0';
    std::strncpy(info->name, path, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    return true;
#else
    std::ifstream maps("/proc/self/maps");
    if (!maps) {
        throw std::runtime_error("sceSysmoduleGetModuleInfoForUnwind: failed to open /proc/self/maps");
    }
    std::string line;
    while (std::getline(maps, line)) {
        std::uint64_t start = 0;
        std::uint64_t end = 0;
        char perms[8] = {};
        std::uint64_t offset = 0;
        unsigned int devMajor = 0;
        unsigned int devMinor = 0;
        std::uint64_t inode = 0;
        char path[4096] = {};
        int parsed = std::sscanf(
            line.c_str(),
            "%llx-%llx %7s %llx %x:%x %llu %4095s",
            (unsigned long long*)&start,
            (unsigned long long*)&end,
            perms,
            (unsigned long long*)&offset,
            &devMajor,
            &devMinor,
            (unsigned long long*)&inode,
            path
        );
        if (parsed < 7 || addr < start || addr >= end) {
            continue;
        }
        info->st_size = sizeof(ModuleInfoForUnwind);
        std::strncpy(info->name, parsed >= 8 ? path : "", sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        info->eh_frame_hdr_addr = 0;
        info->eh_frame_addr = 0;
        info->eh_frame_size = 0;
        info->seg0_addr = start;
        info->seg0_size = end - start;
        return true;
    }
    return false;
#endif
}

}

extern "C" {

/** Fills unwind info for the module containing `addr`. Returns 0; throws when the address is in no module (pre-existing). */
int APS5_VABI sceSysmoduleGetModuleInfoForUnwind(std::uint64_t addr, int flags, ModuleInfoForUnwind* info) {
    (void)flags;
    if (!fillModuleInfoForUnwind(addr, info)) {
        throw std::runtime_error("sceSysmoduleGetModuleInfoForUnwind: address not found");
    }
    return 0;
}

/** Returns 0 when `id` is loaded or unknown to the table (logged), 0x80A90002 when known but not loaded. */
int APS5_VABI sceSysmoduleIsLoaded(std::uint16_t id) {
    if (id == 0) {
        throw std::runtime_error("sceSysmoduleIsLoaded: invalid id 0");
    }
    if (!findModuleName(id)) {
        APS5_LOG_OUT("unknown id: %u", static_cast<unsigned>(id));
        // throw std::runtime_error(std::string("sceSysmoduleIsLoaded: unknown id ") + std::to_string(id));
        return 0;
    }
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gLoadCount.find(id);
    if (it == gLoadCount.end() || it->second < 1) {
        return 0x80A90002;
    }
    return 0;
}

/** Marks `id` loaded (reference counted). Unknown ids are logged and accepted. Always returns 0 (id 0 throws, pre-existing). */
int APS5_VABI sceSysmoduleLoadModule(std::uint16_t id) {
    if (id == 0) {
        throw std::runtime_error("sceSysmoduleLoadModule: invalid id 0");
    }
    if (!findModuleName(id)) {
        APS5_LOG_OUT("unknown id: %u", static_cast<unsigned>(id));
        // throw std::runtime_error(std::string("sceSysmoduleLoadModule: unknown id ") + std::to_string(id));
        return 0;
    }
    std::lock_guard<std::mutex> lock(gMutex);
    gLoadCount[id]++;
    return 0;
}

/** Internal variant of LoadModule that also reports 0 through `ret`. Throws for id 0 or an unknown id (pre-existing). */
int APS5_VABI sceSysmoduleLoadModuleInternalWithArg(std::uint32_t id, int argc, void* argv, std::uint64_t unk, int* ret) {
    (void)argc;
    (void)argv;
    (void)unk;
    if ((id & 0x7fffffffu) == 0) {
        throw std::runtime_error("sceSysmoduleLoadModuleInternalWithArg: invalid id 0");
    }
    if (!findModuleName(id)) {
        throw std::runtime_error(std::string("sceSysmoduleLoadModuleInternalWithArg: unknown id ") + std::to_string(id));
    }
    std::lock_guard<std::mutex> lock(gMutex);
    gLoadCount[id]++;
    if (ret) {
        *ret = 0;
    }
    return 0;
}

/** Drops one reference to `id`. Returns 0; unknown ids are logged and treated as a no-op; 0x80A90003 when a known id is not loaded. */
int APS5_VABI sceSysmoduleUnloadModule(std::uint16_t id) {
    if (id == 0) {
        throw std::runtime_error("sceSysmoduleUnloadModule: invalid id 0");
    }
    if (!findModuleName(id)) {
        // Same policy as sceSysmoduleLoadModule/IsLoaded: an id the project does not know was accepted
        // on load, so its unload is a no-op (it must not throw across the APS5_VABI boundary).
        APS5_LOG_OUT("unknown id: %u", static_cast<unsigned>(id));
        return 0;
    }
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gLoadCount.find(id);
    if (it == gLoadCount.end() || it->second < 1) {
        return 0x80A90003;
    }
    it->second--;
    return 0;
}

}
