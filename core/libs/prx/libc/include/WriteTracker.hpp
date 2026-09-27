#ifndef CORE_LIBS_PRX_LIBC_WRITETRACKER_HPP
#define CORE_LIBS_PRX_LIBC_WRITETRACKER_HPP

#include <cstdint>
#include <span>

// CPU write tracking for guest memory (docs/spec/guest-memory.md).
//
// IWriteTracker is the driver-facing interface of the guest-memory subsystem
// and is defined only here. Block generations for CPU writes (Collect) and
// GPU writes (MarkWritten) live in the tracker; which recorded batch wrote
// which range (writer intervals) lives in the driver's BufferCache
// (docs/spec/gpu-driver.md). NullTracker reports "unknown" for everything so
// driver code can link and run before the real trackers land.
namespace PortPS5 {
namespace GuestMemory {

// Half-open byte range [begin, begin + bytes).
struct AddrRange {
    std::uint64_t begin = 0;
    std::uint64_t bytes = 0;
};

// Lock-free page-state read; one byte per 4 KiB page in the real table.
enum class PageState : std::uint8_t {
    NotGuest = 0,
    Uncommitted,
    ReadOnly,
    ReadWrite,
};

// Opaque pin on registry ranges; pins++ per overlapped Range while held.
struct PinToken {
    std::uint64_t value = 0;
    constexpr bool operator==(const PinToken&) const = default;
};

// Lands pending GPU writes to [address, address + bytes) before a CPU read
// of a tracked range proceeds. Runs with no tracker lock held.
using FlushHook = void (*)(void* context, std::uint64_t address, std::uint64_t bytes);

struct IWriteTracker {
    virtual ~IWriteTracker() = default;
    // Stamps CPU-dirty blocks, returning the newest block generation in the
    // range; 0 means unknown, and the caller compares bytes instead.
    virtual std::uint64_t Collect(std::uint64_t address, std::uint64_t bytes) = 0;
    virtual PinToken Pin(std::span<const AddrRange> ranges) = 0;
    virtual void Unpin(PinToken token) noexcept = 0;
    virtual PageState PageStateAt(std::uint64_t address) const = 0;
    // Reports GPU-written bytes, returning the new generation.
    virtual std::uint64_t MarkWritten(std::uint64_t address, std::uint64_t bytes) = 0;
    virtual void SetFlushHook(FlushHook hook, void* context) = 0;
};

// Always reports "unknown": for non-Windows builds and tracker-unavailable
// paths, and as a stand-in while the real trackers land.
class NullTracker final : public IWriteTracker {
public:
    std::uint64_t Collect(std::uint64_t, std::uint64_t) override { return 0; }
    PinToken Pin(std::span<const AddrRange>) override { return {}; }
    void Unpin(PinToken) noexcept override {}
    PageState PageStateAt(std::uint64_t) const override { return PageState::NotGuest; }
    std::uint64_t MarkWritten(std::uint64_t, std::uint64_t) override { return 0; }
    void SetFlushHook(FlushHook, void*) override {}
};

}  // namespace GuestMemory
}  // namespace PortPS5

#endif
