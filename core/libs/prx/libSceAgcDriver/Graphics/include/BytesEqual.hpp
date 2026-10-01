// core/libs/prx/libSceAgcDriver/Graphics/include/BytesEqual.hpp
//
// Subsystem: AGC driver, per-draw prepare path (docs/spec/gpu-driver.md, "Per-draw CPU cost").
// Purpose: a bounded equality compare for large guest snapshots, used by the TextureCache to revalidate a
// cached texture against guest memory on every draw. The result is exactly `memcmp(a, b, n) == 0`; only the
// speed differs (SSE2, four 16-byte loads per iteration, early exit on the first differing block).
// Threading: pure function, no state, no synchronisation. Guest memory can be written by guest threads while this
// runs, exactly as with the memcmp it replaces; formally that is a data race, so a result taken during a concurrent
// write only means "equal at some instant". The TextureCache accepts that: a write it misses is seen by the next
// revalidation, and a caller that needs a stable answer must exclude the writers itself.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BYTESEQUAL_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BYTESEQUAL_HPP

#include <cstddef>
#include <cstring>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace AgcDriver::Graphics {

/**
 * @brief Reports whether two byte ranges of equal length hold identical contents.
 * @param first First range; must be readable for `bytes` bytes (any alignment).
 * @param second Second range; must be readable for `bytes` bytes (any alignment).
 * @param bytes Length of both ranges; zero compares equal.
 * @return true when every byte matches, exactly as `std::memcmp(first, second, bytes) == 0`.
 */
inline bool BytesEqual(const void* first, const void* second, std::size_t bytes) noexcept {
#if defined(__SSE2__)
    const auto* a = static_cast<const unsigned char*>(first);
    const auto* b = static_cast<const unsigned char*>(second);
    // Unaligned loads (loadu) because guest texture bases and snapshot vectors have no shared alignment.
    // Per 64-byte block the four XORs are OR-folded so there is one branch per block, not four.
    while (bytes >= 64) {
        const auto diff0 = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(b)));
        const auto diff1 = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + 16)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + 16)));
        const auto diff2 = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + 32)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + 32)));
        const auto diff3 = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + 48)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + 48)));
        const auto folded = _mm_or_si128(_mm_or_si128(diff0, diff1), _mm_or_si128(diff2, diff3));
        // cmpeq against zero sets every byte to 0xFF only when all 16 bytes of `folded` are zero.
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(folded, _mm_setzero_si128())) != 0xFFFF) return false;
        a += 64;
        b += 64;
        bytes -= 64;
    }
    // Tail (under 64 bytes) goes through memcmp; it is small and keeps the exact-semantics guarantee trivial.
    return bytes == 0 || std::memcmp(a, b, bytes) == 0;
#else
    return bytes == 0 || std::memcmp(first, second, bytes) == 0;
#endif
}

}

#endif
