// core/libs/prx/libScePlayGo/Export.cpp
// Offline libScePlayGo: the game is fully installed on the host, so every chunk is local and nothing
// is ever pending. The chunk set comes from the title's own /app0/playgo-chunkdefs.xml (read when
// scePlayGoOpen runs, never cached across opens), not from any per-title table.
//
// Error codes, argument order of the checks and the optional-chunk masks follow KytyPS5
// src/libs/libPlayGo.cpp and errno.h (GPL-2.0) and shadPS4 playgo_types.h (GPL-2.0-or-later). The
// codes are the SCE_PLAYGO_ERROR table (0x80B2xxxx); the handle is a process-wide constant.
// All exports are APS5_VABI + noexcept and serialise on one mutex.

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>

#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "PlayGoInternal.hpp"

namespace {

constexpr int kErrInvalidArgument = static_cast<int>(0x80B20004u);
constexpr int kErrBadHandle = static_cast<int>(0x80B20009u);
constexpr int kErrBadPointer = static_cast<int>(0x80B2000Au);
constexpr int kErrBadSize = static_cast<int>(0x80B2000Bu);
constexpr int kErrBadChunkId = static_cast<int>(0x80B2000Cu);
constexpr int kErrBadSpeed = static_cast<int>(0x80B2000Du);
constexpr int kErrBadLocus = static_cast<int>(0x80B20010u);
constexpr int kErrBadOptionalType = static_cast<int>(0x80B20024u);

constexpr int kHandle = 1;
constexpr std::int8_t kLocusNotDownloaded = 0;
constexpr std::int8_t kLocusLocalSlow = 2;
constexpr std::int8_t kLocusLocalFast = 3;
constexpr std::int32_t kInstallSpeedFull = 2;
constexpr std::int32_t kOptionalLanguage = 0;
constexpr std::int32_t kOptionalScenario = 1;
constexpr std::uint64_t kLanguageMaskAll = ~0ull;
constexpr std::uint64_t kScenarioMaskAll = 0x1full;

static_assert(sizeof(PlayGoToDo) == 4 && sizeof(PlayGoProgress) == 16 && sizeof(PlayGoOptionalChunk) == 8,
              "PlayGo guest structs");

std::mutex g_lock;
std::set<std::uint16_t> g_chunks = {0};
std::int32_t g_installSpeed = kInstallSpeedFull;

bool ChunkValid(std::uint16_t id) { return g_chunks.contains(id); }

int CheckHandle(int handle) { return handle == kHandle ? 0 : kErrBadHandle; }

/** Validates a guest chunk-id array: pointer, size and membership in the chunk set. */
int CheckChunks(const std::uint16_t* ids, std::uint32_t count) {
    if (ids == nullptr) {
        return kErrBadPointer;
    }
    if (count == 0) {
        return kErrBadSize;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!ChunkValid(ids[i])) {
            return kErrBadChunkId;
        }
    }
    return 0;
}

bool ValidLocus(std::int8_t locus) {
    return locus == kLocusNotDownloaded || locus == kLocusLocalSlow || locus == kLocusLocalFast;
}

int CheckOptionalType(std::int32_t type) {
    return (type == kOptionalLanguage || type == kOptionalScenario) ? 0 : kErrBadOptionalType;
}

}  // namespace

std::set<std::uint16_t> PlayGoParseChunkDefs(const std::string& xml) {
    std::set<std::uint16_t> result = {0};
    const auto number = [&xml](std::size_t pos, std::uint64_t& value) {
        // Reads a run of decimal digits at pos; false for none or a value that cannot be a chunk id.
        std::size_t end = pos;
        value = 0;
        while (end < xml.size() && xml[end] >= '0' && xml[end] <= '9' && end - pos < 6) {
            value = value * 10 + static_cast<std::uint64_t>(xml[end] - '0');
            ++end;
        }
        return end > pos && value <= 0xFFFF;
    };
    // Every <chunk ... id="N" ...> tag contributes its id.
    for (std::size_t tag = xml.find("<chunk"); tag != std::string::npos; tag = xml.find("<chunk", tag + 1)) {
        const char after = tag + 6 < xml.size() ? xml[tag + 6] : '\0';
        if (after != ' ' && after != '\t' && after != '\n' && after != '\r') {
            continue;  // <chunks> and other longer element names.
        }
        const std::size_t close = xml.find('>', tag);
        const std::size_t attr = xml.find(" id=\"", tag);
        std::uint64_t value = 0;
        if (close != std::string::npos && attr != std::string::npos && attr < close && number(attr + 5, value)) {
            result.insert(static_cast<std::uint16_t>(value));
        }
    }
    // default_chunk="N" names the last chunk of the always-present range 0..N.
    const std::size_t def = xml.find("default_chunk=\"");
    std::uint64_t last = 0;
    if (def != std::string::npos && number(def + 15, last)) {
        for (std::uint64_t id = 0; id <= last; ++id) {
            result.insert(static_cast<std::uint16_t>(id));
        }
    }
    return result;
}

extern "C" {

/**
 * Initializes PlayGo from a guest SceNpPlayGoInitParams.
 * Returns 0, or BAD_POINTER for a null block or buffer. The buffer is not used (no downloads), and
 * its size is not enforced: KytyPS5 demands 2 MiB, which is unverified and would only add failures.
 */
int APS5_VABI scePlayGoInitialize(const PlayGoInitParams* init) noexcept {
    if (init == nullptr || init->buf_addr == nullptr) {
        return kErrBadPointer;
    }
    return 0;
}

/** Terminates PlayGo. Always returns 0. */
int APS5_VABI scePlayGoTerminate(void) noexcept { return 0; }

/**
 * Opens PlayGo and loads the chunk set from /app0/playgo-chunkdefs.xml.
 * Returns 0 and the process-wide handle; BAD_POINTER for a null out pointer; INVALID_ARGUMENT when
 * `param` is non-null. A missing file leaves the set {0}: a dump may omit it, and failing the open
 * would block a title that only needs chunk 0 (inference, noted in docs/spec/save-data.md).
 */
int APS5_VABI scePlayGoOpen(int* out_handle, const void* param) noexcept {
    if (out_handle == nullptr) {
        return kErrBadPointer;
    }
    if (param != nullptr) {
        return kErrInvalidArgument;
    }
    std::set<std::uint16_t> chunks = {0};
    std::ifstream file(ResolvePath_nid_no_patch("/app0/playgo-chunkdefs.xml"), std::ios::binary);
    if (file) {
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        chunks = PlayGoParseChunkDefs(text);
    }
    std::lock_guard lock(g_lock);
    g_chunks = std::move(chunks);
    *out_handle = kHandle;
    return 0;
}

/** Closes the handle. Returns 0, or BAD_HANDLE for any other value. */
int APS5_VABI scePlayGoClose(int handle) noexcept { return CheckHandle(handle); }

/**
 * Lists the chunk ids in ascending order, at most `number_of_entries`.
 * Returns 0, BAD_HANDLE, BAD_POINTER (null count, or null list with a non-zero capacity).
 */
int APS5_VABI scePlayGoGetChunkId(int handle, std::uint16_t* out_chunk_id_list, std::uint32_t number_of_entries,
                                  std::uint32_t* out_entries) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (out_entries == nullptr || (number_of_entries != 0 && out_chunk_id_list == nullptr)) return kErrBadPointer;
    std::lock_guard lock(g_lock);
    std::uint32_t written = 0;
    for (const std::uint16_t id : g_chunks) {
        if (written == number_of_entries) break;
        out_chunk_id_list[written++] = id;
    }
    *out_entries = written;
    return 0;
}

/**
 * Lists the installed chunk ids. Everything is installed, so this equals scePlayGoGetChunkId.
 * Returns the same codes as scePlayGoGetChunkId.
 */
int APS5_VABI scePlayGoGetInstallChunkId(int handle, std::uint16_t* out_chunk_id_list, std::uint32_t number_of_entries,
                                         std::uint32_t* out_entries) noexcept {
    return scePlayGoGetChunkId(handle, out_chunk_id_list, number_of_entries, out_entries);
}

/** Reports 0 (nothing left to download). Returns 0, BAD_HANDLE, BAD_POINTER, BAD_SIZE or BAD_CHUNK_ID. */
int APS5_VABI scePlayGoGetEta(int handle, const std::uint16_t* chunk_ids, std::uint32_t number_of_entries,
                              std::int64_t* out_eta) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (chunk_ids == nullptr || out_eta == nullptr) return kErrBadPointer;
    std::lock_guard lock(g_lock);
    if (const int e = CheckChunks(chunk_ids, number_of_entries)) return e;
    *out_eta = 0;
    return 0;
}

/** Returns the install speed last set (default FULL = 2). Returns 0, BAD_HANDLE or BAD_POINTER. */
int APS5_VABI scePlayGoGetInstallSpeed(int handle, std::int32_t* out_speed) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (out_speed == nullptr) return kErrBadPointer;
    std::lock_guard lock(g_lock);
    *out_speed = g_installSpeed;
    return 0;
}

/** Sets the install speed (0..2). Returns 0, BAD_HANDLE or BAD_SPEED. */
int APS5_VABI scePlayGoSetInstallSpeed(int handle, std::int32_t speed) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (speed < 0 || speed > 2) return kErrBadSpeed;
    std::lock_guard lock(g_lock);
    g_installSpeed = speed;
    return 0;
}

/** Reports every language as present. Returns 0, BAD_HANDLE or BAD_POINTER. */
int APS5_VABI scePlayGoGetLanguageMask(int handle, std::uint64_t* out_language_mask) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (out_language_mask == nullptr) return kErrBadPointer;
    *out_language_mask = kLanguageMaskAll;
    return 0;
}

/** Reports LOCAL_FAST for every valid chunk. Returns 0, BAD_HANDLE, BAD_POINTER, BAD_SIZE or BAD_CHUNK_ID. */
int APS5_VABI scePlayGoGetLocus(int handle, const std::uint16_t* chunk_ids, std::uint32_t number_of_entries,
                                std::int8_t* out_loci) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (chunk_ids == nullptr || out_loci == nullptr) return kErrBadPointer;
    std::lock_guard lock(g_lock);
    if (const int e = CheckChunks(chunk_ids, number_of_entries)) return e;
    std::fill_n(out_loci, number_of_entries, kLocusLocalFast);
    return 0;
}

/** Reports the whole request as downloaded. Returns 0 or the CheckChunks codes. */
int APS5_VABI scePlayGoGetProgress(int handle, const std::uint16_t* chunk_ids, std::uint32_t number_of_entries,
                                   PlayGoProgress* out_progress) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (chunk_ids == nullptr || out_progress == nullptr) return kErrBadPointer;
    std::lock_guard lock(g_lock);
    if (const int e = CheckChunks(chunk_ids, number_of_entries)) return e;
    out_progress->progress_size = number_of_entries;
    out_progress->total_size = number_of_entries;
    return 0;
}

/** Returns an empty to-do list. Returns 0, BAD_HANDLE or BAD_POINTER. */
int APS5_VABI scePlayGoGetToDoList(int handle, PlayGoToDo* out_todo_list, std::uint32_t number_of_entries,
                                   std::uint32_t* out_entries) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (out_entries == nullptr || (number_of_entries != 0 && out_todo_list == nullptr)) return kErrBadPointer;
    *out_entries = 0;
    return 0;
}

/** Accepts a to-do list (nothing to download). Returns 0, BAD_HANDLE, BAD_POINTER, BAD_SIZE, BAD_CHUNK_ID or BAD_LOCUS. */
int APS5_VABI scePlayGoSetToDoList(int handle, const PlayGoToDo* todo_list, std::uint32_t number_of_entries) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (todo_list == nullptr) return kErrBadPointer;
    if (number_of_entries == 0) return kErrBadSize;
    std::lock_guard lock(g_lock);
    for (std::uint32_t i = 0; i < number_of_entries; ++i) {
        if (!ChunkValid(todo_list[i].chunk_id)) return kErrBadChunkId;
        if (!ValidLocus(todo_list[i].locus)) return kErrBadLocus;
    }
    return 0;
}

/** Accepts a prefetch request (already local). Returns 0, BAD_HANDLE, BAD_POINTER, BAD_SIZE, BAD_LOCUS or BAD_CHUNK_ID. */
int APS5_VABI scePlayGoPrefetch(int handle, const std::uint16_t* chunk_ids, std::uint32_t number_of_entries,
                                std::int8_t minimum_locus) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (chunk_ids == nullptr) return kErrBadPointer;
    if (number_of_entries == 0) return kErrBadSize;
    if (!ValidLocus(minimum_locus)) return kErrBadLocus;
    std::lock_guard lock(g_lock);
    return CheckChunks(chunk_ids, number_of_entries);
}

/** Reports the optional chunks of `type` (0 language, 1 scenario) as all present. Returns 0, BAD_HANDLE, BAD_POINTER or BAD_OPTIONAL_TYPE. */
int APS5_VABI scePlayGoGetOptionalChunk(int handle, std::int32_t type, PlayGoOptionalChunk* option) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (option == nullptr) return kErrBadPointer;
    if (const int e = CheckOptionalType(type)) return e;
    option->bitmask = type == kOptionalLanguage ? kLanguageMaskAll : kScenarioMaskAll;
    return 0;
}

/** Same answer as scePlayGoGetOptionalChunk: the supported set equals the present set offline. */
int APS5_VABI scePlayGoGetSupportedOptionalChunk(int handle, std::int32_t type, PlayGoOptionalChunk* option) noexcept {
    return scePlayGoGetOptionalChunk(handle, type, option);
}

/** Accepts an optional-chunk prefetch (already local). Returns 0, BAD_HANDLE, BAD_POINTER or BAD_OPTIONAL_TYPE. */
int APS5_VABI scePlayGoPrefetchOptionalChunk(int handle, std::int32_t type, const PlayGoOptionalChunk* option) noexcept {
    if (const int e = CheckHandle(handle)) return e;
    if (option == nullptr) return kErrBadPointer;
    return CheckOptionalType(type);
}

}  // extern "C"
