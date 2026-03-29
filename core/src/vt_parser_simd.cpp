#include "vt_parser_simd.h"

#include <cstdint>
#include <cstddef>

#if defined(_MSC_VER)
#include <intrin.h>
#include <immintrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#include <cpuid.h>
#endif

namespace termcore {

// --- Scalar fallback ---

static size_t scanPrintableAsciiScalar(const uint8_t* data, size_t len) {
    size_t i = 0;
    for (; i < len; ++i) {
        uint8_t b = data[i];
        if (b < 0x20 || b > 0x7E) break;
    }
    return i;
}

// --- Trailing-zero-count helper ---

static inline unsigned countTrailingZeros(unsigned mask) {
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward(&idx, mask);
    return static_cast<unsigned>(idx);
#else
    return static_cast<unsigned>(__builtin_ctz(mask));
#endif
}

// --- SSE2 (16 bytes at a time) ---

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
static size_t scanPrintableAsciiSse2(const uint8_t* data, size_t len) {
    size_t i = 0;

    const __m128i threshold = _mm_set1_epi8(0x20);
    const __m128i del_val = _mm_set1_epi8(0x7F);

    for (; i + 16 <= len; i += 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
        // Signed comparison: bytes >= 0x80 are negative, so < 0x20 catches
        // both C0 controls (0x00-0x1F) and high bytes (0x80-0xFF)
        __m128i ctrl = _mm_cmplt_epi8(v, threshold);
        __m128i del = _mm_cmpeq_epi8(v, del_val);
        __m128i bad = _mm_or_si128(ctrl, del);
        int mask = _mm_movemask_epi8(bad);

        if (mask != 0)
            return i + countTrailingZeros(static_cast<unsigned>(mask));
    }

    // Scalar tail
    for (; i < len; ++i) {
        uint8_t b = data[i];
        if (b < 0x20 || b > 0x7E) break;
    }
    return i;
}
#endif

// --- AVX2 (32 bytes at a time) ---

#if defined(__AVX2__) || defined(_MSC_VER)
// On MSVC this file is compiled with /arch:AVX2
static size_t scanPrintableAsciiAvx2(const uint8_t* data, size_t len) {
    size_t i = 0;

    const __m256i threshold = _mm256_set1_epi8(0x20);
    const __m256i del_val = _mm256_set1_epi8(0x7F);

    for (; i + 32 <= len; i += 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
        __m256i ctrl = _mm256_cmpgt_epi8(threshold, v);
        __m256i del = _mm256_cmpeq_epi8(v, del_val);
        __m256i bad = _mm256_or_si256(ctrl, del);
        int mask = _mm256_movemask_epi8(bad);

        if (mask != 0)
            return i + countTrailingZeros(static_cast<unsigned>(mask));
    }

    // Use SSE2 for the remaining 16-31 bytes, then scalar for the tail
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
    return i + scanPrintableAsciiSse2(data + i, len - i);
#else
    return i + scanPrintableAsciiScalar(data + i, len - i);
#endif
}
#endif

// --- Runtime CPU dispatch ---

enum class SimdLevel { Scalar, SSE2, AVX2 };

static SimdLevel detectSimdLevel() {
#if defined(_MSC_VER)
    int cpuInfo[4];
    __cpuid(cpuInfo, 0);
    int maxFunc = cpuInfo[0];
    if (maxFunc >= 7) {
        __cpuidex(cpuInfo, 7, 0);
        if (cpuInfo[1] & (1 << 5)) return SimdLevel::AVX2;  // EBX bit 5
    }
    if (maxFunc >= 1) {
        __cpuid(cpuInfo, 1);
        if (cpuInfo[3] & (1 << 26)) return SimdLevel::SSE2;  // EDX bit 26
    }
    return SimdLevel::Scalar;
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx) && eax >= 7) {
        __cpuid_count(7, 0, eax, ebx, ecx, edx);
        if (ebx & (1 << 5)) return SimdLevel::AVX2;
    }
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        if (edx & (1 << 26)) return SimdLevel::SSE2;
    }
    return SimdLevel::Scalar;
#else
    return SimdLevel::Scalar;
#endif
}

using ScanFn = size_t(*)(const uint8_t*, size_t);

static ScanFn resolveScanFn() {
    switch (detectSimdLevel()) {
#if defined(__AVX2__) || defined(_MSC_VER)
        case SimdLevel::AVX2:  return scanPrintableAsciiAvx2;
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
        case SimdLevel::SSE2:  return scanPrintableAsciiSse2;
#endif
        default:               return scanPrintableAsciiScalar;
    }
}

// Resolved once at process startup
static ScanFn s_scanFn = resolveScanFn();

size_t scanPrintableAscii(const uint8_t* data, size_t len) {
    return s_scanFn(data, len);
}

} // namespace termcore
