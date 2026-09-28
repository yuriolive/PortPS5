// Host thread-local storage test helper — libc scope.
//
// Provides two Touch/Destroyed counters over HostThreadLocal<Value, Tag> used
// by HostThreadLocal.cpp. Invariant violations are recorded in atomics (never
// abort(): this code runs on thread-exit paths where abort would hide the
// racing thread). Compiled into the host_thread_local_tests binary.
//
// Ref: docs/spec/threading.md

#include "prx/libc/include/HostThreadLocal.hpp"

#include <atomic>
#include <cstdlib>
#include <vector>

namespace {
std::atomic<unsigned> destroyed{0};
std::atomic<unsigned> violations{0};
struct Value {
    std::vector<unsigned> data = std::vector<unsigned>(256, 42);
    ~Value() {
        for (auto item : data)
            if (item != 42) ++violations;
        ++destroyed;
    }
};
struct First {};
struct Second {};
}  // namespace

#ifdef _WIN32
#define TEST_EXPORT __declspec(dllexport)
#else
#define TEST_EXPORT
#endif

extern "C" TEST_EXPORT void TouchHostThreadLocal() {
    auto& first = HostThreadLocal<Value, First>();
    auto& second = HostThreadLocal<Value, Second>();
    if (&first == &second || &first != &HostThreadLocal<Value, First>()) ++violations;
}

extern "C" TEST_EXPORT unsigned DestroyedHostThreadLocals() { return destroyed.load(); }

extern "C" TEST_EXPORT unsigned HostThreadLocalViolations() { return violations.load(); }
