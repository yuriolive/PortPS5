// Unit tests for the IWriteTracker interface contract
// (docs/spec/guest-memory.md). Header-only: NullTracker is inline.

#include "prx/libc/include/WriteTracker.hpp"

#include <cstdlib>
#include <type_traits>

namespace {

using namespace PortPS5::GuestMemory;

void Require(bool value) {
    if (!value) {
        std::abort();
    }
}
#define REQUIRE(cond) Require(cond)

void TestNullTracker() {
    NullTracker tracker;
    IWriteTracker& iface = tracker;
    // Unknown range: Collect reports 0 so the caller compares bytes.
    REQUIRE(iface.Collect(0x100000000ULL, 65536) == 0);
    // Pins are inert but well-formed.
    const AddrRange ranges[] = {{0x100000000ULL, 4096}, {0x200000000ULL, 8192}};
    const PinToken token = iface.Pin(ranges);
    REQUIRE(token == PinToken{});
    iface.Unpin(token);
    // Nothing is guest memory here.
    REQUIRE(iface.PageStateAt(0x100000000ULL) == PageState::NotGuest);
    // GPU writes report generation 0 (unknown).
    REQUIRE(iface.MarkWritten(0x100000000ULL, 4096) == 0);
    // A flush hook is accepted and never fires.
    iface.SetFlushHook(nullptr, nullptr);
    REQUIRE(iface.Collect(0x100000000ULL, 65536) == 0);
}

void TestTokenAndStateLayout() {
    // Why asserted: PinToken crosses the tracker/driver boundary by value,
    // and the page-state table stores one byte per 4 KiB page.
    static_assert(std::is_trivially_copyable_v<PinToken>);
    static_assert(sizeof(PageState) == 1);
    REQUIRE(PinToken{} == PinToken{});
    REQUIRE(!(PinToken{1} == PinToken{2}));
}

}  // namespace

int main() {
    TestNullTracker();
    TestTokenAndStateLayout();
    return 0;
}
