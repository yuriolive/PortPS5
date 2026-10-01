/*
 * Thread-local hook through which the active executor resolves guest memory ranges before the
 * AGC driver reads or writes them. Owned by the Execution subsystem; one hook per thread, defined
 * once in GuestMemory.cpp so every module sees the same state (see the class comment).
 */
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_MEMORYACCESSSCOPE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_MEMORYACCESSSCOPE_HPP

#include <cstddef>
#include <cstdint>

namespace AgcDriver::GuestMemory {

/**
 * @brief RAII hook that lets the active executor resolve guest ranges before the driver touches them.
 *
 * The hook state is thread-local. It is defined once, in GuestMemory.cpp, and reached through
 * Current(): a header-inline thread_local would give every module that includes this header
 * (the driver DLL, and any test executable linking it) its own copy on PE targets, so a scope
 * installed outside the DLL would never be seen by GuestMemory::CheckRange.
 */
class MemoryAccessScope {
public:
    using Resolver = void (*)(void*, std::uint64_t, std::size_t, bool);

    /** @brief Installs @p resolver for this thread until destruction; nests, restoring the previous hook. */
    MemoryAccessScope(void* context, Resolver resolver) : previous(Current()) {
        Current() = {context, resolver};
    }
    ~MemoryAccessScope() { Current() = previous; }
    MemoryAccessScope(const MemoryAccessScope&) = delete;
    MemoryAccessScope& operator=(const MemoryAccessScope&) = delete;

    /** @brief Invokes the active resolver (if any) with the hook suspended, so it can touch guest memory itself. */
    static void Resolve(std::uint64_t address, std::size_t bytes, bool writable) {
        const auto active = Current();
        if (active.resolver == nullptr || bytes == 0) return;
        const MemoryAccessScope suspended(nullptr, nullptr);
        active.resolver(active.context, address, bytes, writable);
    }

private:
    struct State {
        void* context;
        Resolver resolver;
    };
    /** @brief Per-thread hook storage; defined in GuestMemory.cpp (one definition per process). */
    static State& Current();
    State previous;
};

}

#endif
