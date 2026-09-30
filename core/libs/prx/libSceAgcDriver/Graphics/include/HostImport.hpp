// core/libs/prx/libSceAgcDriver/Graphics/include/HostImport.hpp
//
// Host import of guest memory with a staging fallback (docs/spec/gpu-driver.md, "Host-import budget and
// staging"). Gives the Recorder's GPU work a VkBuffer (and device address) for a guest byte range:
//  - Import: the guest allocation itself, via VK_EXT_external_memory_host. No copies; GPU stores land in
//    guest memory directly. Limited by a byte budget; least-recently-used imports are evicted, never
//    while a recorded batch that used them is unfinished.
//  - Staging: a host-visible copy, refreshed from guest memory when the IWriteTracker generation moved,
//    and written back (completion action) after the batch that wrote it finished.
// A refused or impossible import changes performance only, never results.
//
// Threading: Bind takes a Recorder::Scope and then one internal mutex, so the lock order is always
// Recorder -> HostImport. Completion actions registered on the Recorder run under the Recorder lock and
// touch only the tracker and guest memory, never this object's mutex.
// Lifetime: destroy before the Recorder and before the Vulkan device; ~HostImport syncs the Recorder.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_HOSTIMPORT_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_HOSTIMPORT_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libc/include/WriteTracker.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

class Buffer;

/** @brief Whether GPU work will only read the guest range or may also write it. */
enum class GuestAccess { Read, Write };

/** @brief Result code of HostImport::Bind (no exceptions for guest-visible failures). */
enum class BindStatus {
    Ok,
    InvalidRange,  ///< empty range, null address or address overflow
    NotMapped,     ///< a page of the range is unmapped or lacks the needed permission
    OutOfMemory,   ///< neither an import nor a staging buffer could be created
    OpenBatchWrites,  ///< the open batch writes part of the range and no exact staged copy exists: Submit, then retry
};

/** @brief A guest range as the GPU sees it. */
struct GuestBinding {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;           ///< byte offset of the range inside `buffer`
    VkDeviceSize bytes = 0;            ///< length of the range
    VkDeviceAddress address = 0;       ///< device address of the range's first byte (0 without BDA)
    bool imported = false;             ///< true: the guest allocation itself; false: a staging copy
};

struct BindResult {
    BindStatus status = BindStatus::Ok;
    GuestBinding binding;
};

/** @brief Tunables. Both are proposals to be tuned in Milestone 5 (automatic budget). */
struct HostImportOptions {
    /// Total bytes that may be imported at once. 0 disables imports (staging only): the test hook the
    /// spec asks for, and the forced fallback for devices without VK_EXT_external_memory_host.
    std::uint64_t importBudgetBytes = 0;
    /// Total bytes of cached staging buffers before the least recently used are dropped.
    std::uint64_t stagingCacheBytes = 256ull << 20;
};

/** @brief Counters for tests and the results JSON. */
struct HostImportStats {
    std::uint64_t imports = 0;         ///< successful vkAllocateMemory host-pointer imports
    std::uint64_t importHits = 0;      ///< binds served by an existing import
    std::uint64_t importRefusals = 0;  ///< binds that wanted an import but fell back to staging
    std::uint64_t evictions = 0;       ///< imports evicted for budget
    std::uint64_t stagingBinds = 0;
    std::uint64_t stagingUploads = 0;  ///< guest -> staging copies
    std::uint64_t importedBytes = 0;   ///< currently imported
};

class HostImport {
public:
    /**
     * @param context Device handles; externalMemoryHost/hostImportAlignment gate imports.
     * @param recorder Receives write notes, completions and Keep'd staging buffers.
     * @param tracker Generations (Collect), page state and GPU-write reports; must outlive this object.
     */
    HostImport(const Context& context, Recorder& recorder, PortPS5::GuestMemory::IWriteTracker& tracker, const HostImportOptions& options);
    ~HostImport();
    HostImport(const HostImport&) = delete;
    HostImport& operator=(const HostImport&) = delete;

    /**
     * @brief Binds the guest range [address, address + bytes) for GPU use in the open Recorder batch.
     *
     * Imports when possible, otherwise stages. For GuestAccess::Write the range is noted as a pending
     * write of the open batch, and its results reach guest memory (staging: a write-back completion;
     * import: directly, with MarkWritten at completion). The caller must hold a Recorder::Scope while
     * it records work using the binding.
     * @return Status (never throws for bad guest ranges) and, for Ok, the binding.
     */
    BindResult Bind(std::uint64_t address, std::size_t bytes, GuestAccess access);

    /** @return A snapshot of the counters. */
    HostImportStats Stats() const;

private:
    struct Import;
    struct Staging;
    bool tryImport(std::uint64_t address, std::size_t bytes, GuestAccess access, GuestBinding& out);
    BindResult stage(std::uint64_t address, std::size_t bytes, GuestAccess access);
    bool evictFor(std::uint64_t needed);
    void destroy(Import& entry) noexcept;
    void barrierAgainstOpenWrites();
    // Writes this HostImport recorded into the OPEN batch (cleared when the open serial changes): which
    // kind of producer wrote a range decides whether a later bind may import it or must use its copy.
    struct OpenWrite {
        std::uint64_t begin;
        std::uint64_t end;
        bool imported;
        std::pair<std::uint64_t, std::size_t> key;
    };
    void noteOpenWrite(std::uint64_t address, std::size_t bytes, bool imported);
    bool openStagedWriteOverlaps(std::uint64_t address, std::size_t bytes) const;
    bool openWritesOnlyFromStaging(std::uint64_t address, std::size_t bytes, const std::pair<std::uint64_t, std::size_t>& key) const;
    void afterBind(std::uint64_t address, std::size_t bytes, GuestAccess access, const std::shared_ptr<Buffer>& staged);

    Context context;
    Recorder& recorder;
    PortPS5::GuestMemory::IWriteTracker& tracker;
    HostImportOptions options;
    mutable std::mutex mutex;
    std::map<std::uint64_t, std::unique_ptr<Import>> imports;  // keyed by window begin
    std::map<std::pair<std::uint64_t, std::size_t>, std::unique_ptr<Staging>> stagings;
    std::vector<OpenWrite> openWrites;
    std::uint64_t openWritesSerial = 0;
    std::uint64_t useClock = 0;
    std::uint64_t stagedBytes = 0;
    HostImportStats stats;
};

}

#endif
