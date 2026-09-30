// GCC/Clang __atomic builtin wrappers used by the libc C11/STL atomic exports (RuntimeSupport.cpp).
// All operations are sequentially consistent: the guest passes a memory order, but the strongest order is
// always a valid (if not minimal) implementation of any weaker request, so the order argument is ignored.
#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GCC_ATOMICOPS_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GCC_ATOMICOPS_HPP

inline unsigned int GccAtomicFetchAdd(volatile unsigned int* target, unsigned int value) {
    return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}

inline unsigned int GccAtomicFetchSub(volatile unsigned int* target, unsigned int value) {
    return __atomic_fetch_sub(target, value, __ATOMIC_SEQ_CST);
}

/// Weak compare-and-swap (may fail spuriously; callers loop). On failure `*expected` receives the
/// observed value. Sequentially consistent on both paths.
inline bool GccAtomicCompareExchangeWeak(volatile unsigned int* target, unsigned int* expected, unsigned int desired) {
    return __atomic_compare_exchange_n(target, expected, desired, true, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

inline unsigned int GccAtomicLoad(volatile unsigned int* target) {
    return __atomic_load_n(target, __ATOMIC_SEQ_CST);
}

#endif
