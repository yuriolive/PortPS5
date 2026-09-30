// core/libs/prx/libSceAgcDriver/Graphics/src/HostImport.cpp
//
// Host import + staging fallback for guest memory (see HostImport.hpp, docs/spec/gpu-driver.md).
// Adapted from the ideas of AnyPS5 8a69fefe Graphics/src/GuestBufferMemory.cpp (host imports, persistent
// staging, write-back on completion) on top of PortPS5's IWriteTracker (Collect/MarkWritten/PageStateAt).
//
// Invariants:
//  - An import covers a window aligned to minImportedHostPointerAlignment, and every 4 KiB page of the
//    window must be tracked ReadWrite (ReadOnly suffices for read-only binds); otherwise the bind
//    stages. The memory type used must be HOST_COHERENT so the CPU and GPU see each other's stores
//    without explicit flushes.
//  - An import is never destroyed while a batch that used it is unfinished: lastSerial is the serial the
//    open batch will get, eviction needs lastSerial <= Recorder::CompletedSerial().
//  - A staging refresh allocates a NEW buffer and Keeps the old one, so a batch still reading the old
//    copy is never overwritten. Generation 0 from Collect means "unknown": the range is re-uploaded.
//  - Completion actions capture only values and the tracker; they never take the HostImport mutex.
#include "prx/libSceAgcDriver/Graphics/include/HostImport.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>

namespace AgcDriver::Graphics {

namespace {

constexpr std::uint64_t PageBytes = PortPS5::GuestMemory::kPageStatePageBytes;

bool RangeValid(std::uint64_t address, std::size_t bytes) {
    return address != 0 && bytes != 0 && bytes <= std::numeric_limits<std::uint64_t>::max() - address;
}

}

struct HostImport::Import {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    std::uint64_t lastSerial = 0;  // serial of the newest batch that may still use it
    std::uint64_t lastUse = 0;     // LRU clock
};

struct HostImport::Staging {
    std::shared_ptr<Buffer> buffer;
    std::uint64_t generation = 0;
    std::uint64_t lastUse = 0;
    std::size_t bytes = 0;
};

HostImport::HostImport(const Context& ctx, Recorder& rec, PortPS5::GuestMemory::IWriteTracker& trk, const HostImportOptions& opts)
    : context(ctx), recorder(rec), tracker(trk), options(opts) {}

HostImport::~HostImport() {
    // Every batch that may reference an import or staging buffer finishes before they are destroyed.
    try {
        recorder.Sync();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[gpu] host import teardown: %s\n", error.what());
    }
    for (auto& [begin, entry] : imports) destroy(*entry);
    imports.clear();
    stagings.clear();
}

void HostImport::destroy(Import& entry) noexcept {
    if (entry.buffer != VK_NULL_HANDLE) context.Function<PFN_vkDestroyBuffer>("vkDestroyBuffer")(context.device, entry.buffer, nullptr);
    if (entry.memory != VK_NULL_HANDLE) context.Function<PFN_vkFreeMemory>("vkFreeMemory")(context.device, entry.memory, nullptr);
    entry.buffer = VK_NULL_HANDLE;
    entry.memory = VK_NULL_HANDLE;
}

HostImportStats HostImport::Stats() const {
    std::lock_guard lock(mutex);
    return stats;
}

bool HostImport::evictFor(std::uint64_t needed) {
    if (needed > options.importBudgetBytes) return false;
    while (stats.importedBytes + needed > options.importBudgetBytes) {
        const auto completed = recorder.CompletedSerial();
        auto victim = imports.end();
        for (auto it = imports.begin(); it != imports.end(); ++it) {
            // Never while a batch that used it is unfinished.
            if (it->second->lastSerial > completed) continue;
            if (victim == imports.end() || it->second->lastUse < victim->second->lastUse) victim = it;
        }
        if (victim == imports.end()) return false;
        stats.importedBytes -= victim->second->end - victim->second->begin;
        destroy(*victim->second);
        imports.erase(victim);
        ++stats.evictions;
    }
    return true;
}

bool HostImport::tryImport(std::uint64_t address, std::size_t bytes, GuestAccess access, GuestBinding& out) {
    const auto alignment = static_cast<std::uint64_t>(context.hostImportAlignment);
    if (!context.externalMemoryHost || options.importBudgetBytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) return false;
    const std::uint64_t end = address + bytes;
    const std::uint64_t windowBegin = address & ~(alignment - 1);
    const std::uint64_t windowEnd = (end + alignment - 1) & ~(alignment - 1);
    if (windowEnd < end || windowEnd - windowBegin > options.importBudgetBytes) return false;
    // The whole window is imported, not just the range: every page of it must be known-mapped.
    for (std::uint64_t page = windowBegin; page < windowEnd; page += PageBytes) {
        const auto state = tracker.PageStateAt(page);
        const bool ok = state == PortPS5::GuestMemory::PageState::ReadWrite || (access == GuestAccess::Read && state == PortPS5::GuestMemory::PageState::ReadOnly);
        if (!ok) return false;
    }
    const Import* found = nullptr;
    for (const auto& [begin, entry] : imports) {
        if (entry->begin <= windowBegin && windowEnd <= entry->end) {
            found = entry.get();
            break;
        }
    }
    if (found == nullptr) {
        if (!evictFor(windowEnd - windowBegin)) return false;
        auto entry = std::make_unique<Import>();
        entry->begin = windowBegin;
        entry->end = windowEnd;
        try {
            const auto size = windowEnd - windowBegin;
            void* host = reinterpret_cast<void*>(windowBegin);
            VkMemoryHostPointerPropertiesEXT pointer{VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
            Check(context.Function<PFN_vkGetMemoryHostPointerPropertiesEXT>("vkGetMemoryHostPointerPropertiesEXT")(context.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, host, &pointer), "vkGetMemoryHostPointerPropertiesEXT");
            const bool addressable = context.bufferDeviceAddress;
            VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
            external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &external};
            info.size = size;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | (addressable ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0u);
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            Check(context.Function<PFN_vkCreateBuffer>("vkCreateBuffer")(context.device, &info, nullptr, &entry->buffer), "vkCreateBuffer import");
            VkMemoryRequirements requirements{};
            context.Function<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(context.device, entry->buffer, &requirements);
            Require(requirements.size <= size, "imported buffer needs more memory than the window");
            // HOST_COHERENT only: no explicit flush/invalidate exists for imported guest memory.
            const auto mask = requirements.memoryTypeBits & pointer.memoryTypeBits;
            constexpr VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            std::uint32_t type = context.memory.memoryTypeCount;
            for (std::uint32_t i = 0; i < context.memory.memoryTypeCount; ++i) {
                if ((mask & (1u << i)) != 0 && (context.memory.memoryTypes[i].propertyFlags & wanted) == wanted) {
                    type = i;
                    break;
                }
            }
            Require(type < context.memory.memoryTypeCount, "no coherent host-importable memory type");
            const VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, 0};
            VkImportMemoryHostPointerInfoEXT import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT, addressable ? &flags : nullptr, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, host};
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import};
            allocation.allocationSize = size;
            allocation.memoryTypeIndex = type;
            Check(context.Function<PFN_vkAllocateMemory>("vkAllocateMemory")(context.device, &allocation, nullptr, &entry->memory), "vkAllocateMemory host import");
            Check(context.Function<PFN_vkBindBufferMemory>("vkBindBufferMemory")(context.device, entry->buffer, entry->memory, 0), "vkBindBufferMemory import");
            if (addressable) {
                const VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, entry->buffer};
                entry->address = context.Function<PFN_vkGetBufferDeviceAddressKHR>("vkGetBufferDeviceAddressKHR")(context.device, &addressInfo);
            }
        } catch (const std::exception&) {
            // A refused import changes performance only: the caller stages instead.
            destroy(*entry);
            return false;
        }
        found = entry.get();
        stats.importedBytes += windowEnd - windowBegin;
        ++stats.imports;
        imports.emplace(windowBegin, std::move(entry));
    } else {
        ++stats.importHits;
    }
    auto* entry = imports.at(found->begin).get();
    entry->lastUse = ++useClock;
    // The batch that will record this bind's work is the next one submitted.
    entry->lastSerial = recorder.Submissions() + 1;
    out.buffer = entry->buffer;
    out.offset = address - entry->begin;
    out.bytes = bytes;
    out.address = entry->address != 0 ? entry->address + out.offset : 0;
    out.imported = true;
    return true;
}

BindResult HostImport::stage(std::uint64_t address, std::size_t bytes, GuestAccess access) {
    BindResult result;
    try {
        GuestMemory::CheckRange(reinterpret_cast<const void*>(address), bytes, 1, access == GuestAccess::Write);
    } catch (const std::exception&) {
        result.status = BindStatus::NotMapped;
        return result;
    }
    const auto key = std::make_pair(address, bytes);
    auto found = stagings.find(key);
    if (recorder.OpenWriteOverlaps(address, bytes)) {
        // The producer is in the batch being recorded right now. Collect below would run the flush hook,
        // which would end and submit that batch under the caller, who is still recording into it (the
        // Recorder::Scope contract). Serve the exact staged copy the producer wrote (ordered by a
        // barrier) or report that a Submit is needed first; never sync implicitly here.
        if (found == stagings.end() || found->second->buffer == nullptr || !openWritesOnlyFromStaging(address, bytes, key)) {
            // No copy, or the open batch's writes to the range did not all go into THIS copy (another
            // staged range, an import, or a write this object did not make): its bytes would be stale.
            result.status = BindStatus::OpenBatchWrites;
            return result;
        }
        auto& entry = *found->second;
        entry.lastUse = ++useClock;
        recorder.Keep(entry.buffer);
        result.binding.buffer = entry.buffer->Handle();
        result.binding.bytes = bytes;
        result.binding.address = context.bufferDeviceAddress ? entry.buffer->DeviceAddress() : 0;
        ++stats.stagingBinds;
        barrierAgainstOpenWrites();
        afterBind(address, bytes, access, entry.buffer);
        return result;
    }
    // Land in-flight writers (their write-backs reach guest memory only at completion) without relying
    // on an activated flush hook, and without submitting the open batch.
    if (recorder.InFlightWriteOverlaps(address, bytes)) recorder.SyncInFlightWrites(address, bytes);
    // Collect: it runs the flush hook (if any), then stamps CPU-dirty blocks.
    const auto generation = tracker.Collect(address, bytes);
    found = stagings.find(key);
    const bool upload = found == stagings.end() || generation == 0 || found->second->generation != generation;
    try {
        if (found == stagings.end()) {
            found = stagings.emplace(key, std::make_unique<Staging>()).first;
            found->second->bytes = bytes;
            stagedBytes += bytes;
        }
        auto& entry = *found->second;
        if (upload) {
            const auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | (context.bufferDeviceAddress ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0u);
            // A fresh buffer: a batch still reading the old copy must not see it overwritten.
            auto fresh = std::make_shared<Buffer>(context, bytes, usage);
            GuestMemory::Read(address, fresh->Bytes().subspan(0, bytes));
            if (entry.buffer != nullptr) recorder.Keep(std::move(entry.buffer));
            entry.buffer = std::move(fresh);
            entry.generation = generation;
            ++stats.stagingUploads;
        }
        entry.lastUse = ++useClock;
        recorder.Keep(entry.buffer);
        result.binding.buffer = entry.buffer->Handle();
        result.binding.offset = 0;
        result.binding.bytes = bytes;
        result.binding.address = context.bufferDeviceAddress ? entry.buffer->DeviceAddress() : 0;
        result.binding.imported = false;
        ++stats.stagingBinds;
        afterBind(address, bytes, access, entry.buffer);
    } catch (const std::exception&) {
        if (found != stagings.end() && found->second->buffer == nullptr) {
            stagedBytes -= found->second->bytes;
            stagings.erase(found);
        }
        result = BindResult{};
        result.status = BindStatus::OutOfMemory;
        return result;
    }
    // Drop the least recently used cached copies past the cap; each was Keep'd by the batch that last
    // bound it, so a batch still using it stays valid.
    while (stagedBytes > options.stagingCacheBytes && stagings.size() > 1) {
        auto victim = stagings.begin();
        for (auto it = stagings.begin(); it != stagings.end(); ++it) {
            if (it->second->lastUse < victim->second->lastUse) victim = it;
        }
        if (victim->first == key) break;
        stagedBytes -= victim->second->bytes;
        stagings.erase(victim);
    }
    return result;
}

void HostImport::afterBind(std::uint64_t address, std::size_t bytes, GuestAccess access, const std::shared_ptr<Buffer>& staged) {
    if (access != GuestAccess::Write) return;
    // Noted first so a CPU read through the flush hook syncs from now on.
    recorder.NotePendingWrite(address, bytes);
    noteOpenWrite(address, bytes, staged == nullptr);
    auto* trk = &tracker;
    auto* owner = &recorder;
    if (staged == nullptr) {
        // Imported: the GPU wrote guest memory directly; only the tracker needs to learn about it.
        recorder.OnComplete([trk, address, bytes] { trk->MarkWritten(address, bytes); });
        return;
    }
    // Staged: write-back after the batch finished, bypassing the flush hook (it runs inside finish()).
    recorder.OnComplete([trk, owner, address, bytes, staged] {
        GuestMemory::CheckRange(reinterpret_cast<const void*>(address), bytes, 1, true);
        std::memcpy(reinterpret_cast<void*>(address), staged->Bytes().data(), bytes);
        trk->MarkWritten(address, bytes);
        // On the OWNING recorder, whether or not it is the active one: a completion label stored on
        // the GPU decides from this ring whether the write-back overwrote it.
        owner->NoteWrittenBackOn(address, bytes);
    });
}

void HostImport::noteOpenWrite(std::uint64_t address, std::size_t bytes, bool imported) {
    const auto serial = recorder.Submissions() + 1;  // the serial the open batch will get
    if (openWritesSerial != serial) {
        openWrites.clear();
        openWritesSerial = serial;
    }
    openWrites.push_back({address, address + bytes, imported, std::make_pair(address, bytes)});
}

bool HostImport::openStagedWriteOverlaps(std::uint64_t address, std::size_t bytes) const {
    if (openWritesSerial != recorder.Submissions() + 1) return false;
    const auto end = address + bytes;
    return std::any_of(openWrites.begin(), openWrites.end(), [&](const OpenWrite& write) { return !write.imported && address < write.end && write.begin < end; });
}

bool HostImport::openWritesOnlyFromStaging(std::uint64_t address, std::size_t bytes, const std::pair<std::uint64_t, std::size_t>& key) const {
    if (openWritesSerial != recorder.Submissions() + 1) return false;
    const auto end = address + bytes;
    std::size_t own = 0;
    for (const auto& write : openWrites) {
        if (!(address < write.end && write.begin < end)) continue;
        if (write.imported || write.key != key) return false;
        ++own;
    }
    // Every write note of the open batch over the range must be one of ours: the Recorder also holds notes
    // made directly (the driver's dispatches) or by another HostImport, whose bytes this copy lacks. Each
    // of our writes adds exactly one note, so equal counts mean no foreign writer.
    return own != 0 && own == recorder.OpenWriteCount(address, bytes);
}

void HostImport::barrierAgainstOpenWrites() {
    // A global read/write memory dependency over everything recorded or submitted before it. Used to
    // make an earlier recorded write visible to this bind's reads, and (Bind, GuestAccess::Write) to
    // order this bind's writes after earlier READS of a reused buffer: pipeline barrier scopes include
    // earlier submissions on the queue, which a bare submission order does not give. A global barrier is
    // cheap and also covers an import window that is a different buffer handle than the earlier user.
    const auto commands = recorder.Commands();
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    context.Function<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

BindResult HostImport::Bind(std::uint64_t address, std::size_t bytes, GuestAccess access) {
    BindResult result;
    if (!RangeValid(address, bytes)) {
        result.status = BindStatus::InvalidRange;
        return result;
    }
    // Recorder lock before the HostImport mutex, always: stage() calls Collect, whose flush hook takes the
    // Recorder lock, so taking them in the other order on another thread would deadlock.
    const Recorder::Scope scope(recorder);
    std::lock_guard lock(mutex);
    result = bindLocked(address, bytes, access);
    // A write bind may reuse a buffer that unfinished GPU work still reads (a cached staging copy whose
    // generation did not move, or an existing import window): order the caller's writes, recorded after
    // this point, behind everything before it. Recorded before the caller's commands, so it is in front.
    if (result.status == BindStatus::Ok && access == GuestAccess::Write) barrierAgainstOpenWrites();
    return result;
}

BindResult HostImport::bindLocked(std::uint64_t address, std::size_t bytes, GuestAccess access) {
    BindResult result;
    GuestBinding imported;
    const bool openOverlap = recorder.OpenWriteOverlaps(address, bytes);
    // An import reads guest memory directly, but a staged writer reaches guest memory only when its batch
    // completes. So: a staged writer in the OPEN batch sends this bind to its staged copy (stage()), and
    // staged writers already in flight are landed first (never submitting the open batch).
    const bool stagedOpenWriter = openOverlap && openStagedWriteOverlaps(address, bytes);
    if (!stagedOpenWriter) {
        if (recorder.InFlightWriteOverlaps(address, bytes)) recorder.SyncInFlightWrites(address, bytes);
        if (tryImport(address, bytes, access, imported)) {
            result.binding = imported;
            if (openOverlap) barrierAgainstOpenWrites();
            afterBind(address, bytes, access, nullptr);
            return result;
        }
    }
    if (context.externalMemoryHost && options.importBudgetBytes != 0) ++stats.importRefusals;
    return stage(address, bytes, access);
}

}
