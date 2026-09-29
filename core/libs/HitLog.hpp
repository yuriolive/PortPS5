// HitLog.hpp: lightweight call-frequency logging helper for PS5 replacement PRX modules.
// Tracks execution hit counts via relaxed atomics to log only the first few invocations
// without flooding logs on hot guest polling paths. Thread-safe and lock-free.
#ifndef CORE_LIBS_HITLOG_HPP
#define CORE_LIBS_HITLOG_HPP

#include <atomic>
#include <cstdio>

// Logs the first three calls of the surrounding call site, then stays silent (keeps guest logs usable).
#define APS5_HIT(tag, ...) \
    do { \
        static std::atomic<int> aps5HitCount{0}; \
        if (aps5HitCount.fetch_add(1, std::memory_order_relaxed) < 3) { \
            std::fprintf(stderr, "[" tag "] " __VA_ARGS__); \
            std::fputc('\n', stderr); \
            std::fflush(stderr); \
        } \
    } while (0)

#endif
