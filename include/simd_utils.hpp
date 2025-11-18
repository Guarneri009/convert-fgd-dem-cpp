#pragma once

#include <cstddef>
#include <cstring>

// Platform detection for SIMD intrinsics
#if defined(__x86_64__) || defined(_M_X64)
#    if defined(__AVX2__)
#        define HAS_AVX2 1
#        include <immintrin.h>
#    endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define HAS_NEON 1
#    include <arm_neon.h>
#endif

// SIMD utilities for faster string processing
namespace fgd_converter::simd {

#if defined(HAS_AVX2)

inline const char* find_char_simd(const char* ptr, const char* end, char target) {
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
inline const char* skip_whitespace_simd(const char* ptr, const char* end) {
    // Scalar version is simpler and often faster for small whitespace runs
    // Use scalar code instead of complex SIMD logic
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
        ++ptr;
    }
    return ptr;
}

#elif defined(HAS_NEON)

inline const char* find_char_simd(const char* ptr, const char* end, char target) {
    if (ptr >= end)
        return nullptr;

    // Use standard memchr which is optimized for ARM NEON
    return static_cast<const char*>(std::memchr(ptr, target, end - ptr));
}

/**
 * @brief Skip whitespace using NEON
 *
 * Quickly skip spaces, tabs, newlines, and carriage returns.
 */
inline const char* skip_whitespace_simd(const char* ptr, const char* end) {
    // Scalar version is simpler and often faster for small whitespace runs
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
        ++ptr;
    }
    return ptr;
}

#else
// Fallback to standard functions for systems without SIMD support
inline const char* find_char_simd(const char* ptr, const char* end, char target) {
    return static_cast<const char*>(std::memchr(ptr, target, end - ptr));
}

inline const char* skip_whitespace_simd(const char* ptr, const char* end) {
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
        ++ptr;
    }
    return ptr;
}
#endif

// Legacy aliases for backward compatibility
inline const char* find_char_avx2(const char* ptr, const char* end, char target) {
    return find_char_simd(ptr, end, target);
}

inline const char* skip_whitespace_avx2(const char* ptr, const char* end) {
    return skip_whitespace_simd(ptr, end);
}

}  // namespace fgd_converter::simd
