#pragma once

#include <cstddef>
#include <cstring>

// SIMDイントリンシクスのプラットフォーム検出
#if defined(__x86_64__) || defined(_M_X64)
#    if defined(__AVX2__)
#        define HAS_AVX2 1
#        include <immintrin.h>
#    endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define HAS_NEON 1
#    include <arm_neon.h>
#endif

// 高速文字列処理用SIMDユーティリティ
namespace fgd_converter::simd {

#if defined(HAS_AVX2)

inline const char* find_char_simd(const char* ptr, const char* end, char target) {
    if (ptr >= end)
        return nullptr;

    // 短い文字列ではmemchrの方が速いことが多い
    // SSE2/AVX2で高度に最適化された標準memchrを使用
    return static_cast<const char*>(std::memchr(ptr, target, end - ptr));
}

/**
 * @brief AVX2を使用して空白をスキップ
 *
 * スペース、タブ、改行、キャリッジリターンを高速にスキップ。
 */
inline const char* skip_whitespace_simd(const char* ptr, const char* end) {
    // スカラー版はシンプルで、短い空白連続には速いことが多い
    // 複雑なSIMDロジックの代わりにスカラーコードを使用
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
        ++ptr;
    }
    return ptr;
}

#elif defined(HAS_NEON)

inline const char* find_char_simd(const char* ptr, const char* end, char target) {
    if (ptr >= end)
        return nullptr;

    // ARM NEON用に最適化された標準memchrを使用
    return static_cast<const char*>(std::memchr(ptr, target, end - ptr));
}

/**
 * @brief NEONを使用して空白をスキップ
 *
 * スペース、タブ、改行、キャリッジリターンを高速にスキップ。
 */
inline const char* skip_whitespace_simd(const char* ptr, const char* end) {
    // スカラー版はシンプルで、短い空白連続には速いことが多い
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
        ++ptr;
    }
    return ptr;
}

#else
// SIMDサポートのないシステム用に標準関数へフォールバック
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

// 後方互換性のためのレガシーエイリアス
inline const char* find_char_avx2(const char* ptr, const char* end, char target) {
    return find_char_simd(ptr, end, target);
}

inline const char* skip_whitespace_avx2(const char* ptr, const char* end) {
    return skip_whitespace_simd(ptr, end);
}

}  // namespace fgd_converter::simd
