#pragma once

#include <cstddef>
#include <cstring>

// SIMD utilities for faster string processing
namespace fgd_converter::simd {

#if defined(__AVX2__)
#    include <immintrin.h>

inline const char* find_char_avx2(const char* ptr, const char* end, char target) {
    if (ptr >= end)
        return nullptr;

    // For small strings, memchr is often faster
    // Use standard memchr which is already highly optimized with SSE2/AVX2
    return static_cast<const char*>(std::memchr(ptr, target, end - ptr));
}

/**
 * @brief Skip whitespace using AVX2
 *
 * Quickly skip spaces, tabs, newlines, and carriage returns.
 */
inline const char* skip_whitespace_avx2(const char* ptr, const char* end) {
    // Scalar version is simpler and often faster for small whitespace runs
    // Use scalar code instead of complex SIMD logic
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
        ++ptr;
    }
    return ptr;
}

#else
// Fallback to standard memchr for non-AVX2 systems
inline const char* find_char_avx2(const char* ptr, const char* end, char target) {
    return static_cast<const char*>(std::memchr(ptr, target, end - ptr));
}

inline const char* skip_whitespace_avx2(const char* ptr, const char* end) {
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
        ++ptr;
    }
    return ptr;
}
#endif

}  // namespace fgd_converter::simd
