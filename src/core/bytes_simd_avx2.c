/*
 * src/core/bytes_simd_avx2.c
 *
 * Optional AVX2 byte-search backend for configured advanced SIMD builds.
 * The scalar API contract remains authoritative.
 */
#include "bytes_internal.h"

#if SL_BYTES_SIMD_AVX2

#include <immintrin.h>
#include <stdint.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

static __m256i sl_avx2_loadu_256(const unsigned char* ptr)
{
    return _mm256_loadu_si256((const __m256i*)ptr);
}

static size_t sl_avx2_first_mask_index(uint32_t mask)
{
    if (mask == 0U) {
        return 32U;
    }

#if defined(_MSC_VER) && !defined(__clang__)
    {
        unsigned long index = 0UL;
        _BitScanForward(&index, mask);
        return (size_t)index;
    }
#else
    return (size_t)__builtin_ctz(mask);
#endif
}

SlStatus sl_bytes_find_avx2(SlBytes bytes, unsigned char needle, SlBytesFindResult* out)
{
    const __m256i needle_vec = _mm256_set1_epi8((char)needle);
    size_t index = 0U;
    SlBytesFindResult result = {.found = false, .index = bytes.length, .value = 0U};

    while (index + 32U <= bytes.length) {
        const __m256i chunk = sl_avx2_loadu_256(bytes.ptr + index);
        const uint32_t match_mask =
            (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, needle_vec));

        if (match_mask != 0U) {
            result.found = true;
            result.index = index + sl_avx2_first_mask_index(match_mask);
            result.value = needle;
            *out = result;
            return sl_status_ok();
        }

        index += 32U;
    }

    for (; index < bytes.length; index += 1U) {
        if (bytes.ptr[index] == needle) {
            result.found = true;
            result.index = index;
            result.value = needle;
            *out = result;
            return sl_status_ok();
        }
    }

    *out = result;
    return sl_status_ok();
}

SlStatus sl_bytes_find_any_avx2(SlBytes bytes, SlBytes needles, SlBytesFindResult* out)
{
    size_t index = 0U;
    SlBytesFindResult result = {.found = false, .index = bytes.length, .value = 0U};

    if (needles.length == 0U) {
        *out = result;
        return sl_status_ok();
    }
    if (needles.length == 1U) {
        return sl_bytes_find_avx2(bytes, needles.ptr[0], out);
    }
    if (needles.length <= 4U) {
        const __m256i needle0 = _mm256_set1_epi8((char)needles.ptr[0]);
        const __m256i needle1 = _mm256_set1_epi8((char)needles.ptr[1]);
        const __m256i needle2 =
            needles.length > 2U ? _mm256_set1_epi8((char)needles.ptr[2]) : _mm256_setzero_si256();
        const __m256i needle3 =
            needles.length > 3U ? _mm256_set1_epi8((char)needles.ptr[3]) : _mm256_setzero_si256();

        while (index + 32U <= bytes.length) {
            const __m256i chunk = sl_avx2_loadu_256(bytes.ptr + index);
            uint32_t match_mask =
                (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, needle0)) |
                (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, needle1));

            if (needles.length > 2U) {
                match_mask |= (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, needle2));
            }
            if (needles.length > 3U) {
                match_mask |= (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, needle3));
            }

            if (match_mask != 0U) {
                size_t offset = sl_avx2_first_mask_index(match_mask);
                result.found = true;
                result.index = index + offset;
                result.value = bytes.ptr[result.index];
                *out = result;
                return sl_status_ok();
            }

            index += 32U;
        }
    }

    while (index + 32U <= bytes.length) {
        size_t needle_index = 0U;
        uint32_t match_mask = 0U;
        const __m256i chunk = sl_avx2_loadu_256(bytes.ptr + index);

        for (needle_index = 0U; needle_index < needles.length; needle_index += 1U) {
            const __m256i needle = _mm256_set1_epi8((char)needles.ptr[needle_index]);
            match_mask |= (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, needle));
        }

        if (match_mask != 0U) {
            size_t offset = sl_avx2_first_mask_index(match_mask);
            result.found = true;
            result.index = index + offset;
            result.value = bytes.ptr[result.index];
            *out = result;
            return sl_status_ok();
        }

        index += 32U;
    }

    for (; index < bytes.length; index += 1U) {
        size_t needle_index = 0U;

        for (needle_index = 0U; needle_index < needles.length; needle_index += 1U) {
            if (bytes.ptr[index] == needles.ptr[needle_index]) {
                result.found = true;
                result.index = index;
                result.value = bytes.ptr[index];
                *out = result;
                return sl_status_ok();
            }
        }
    }

    *out = result;
    return sl_status_ok();
}

#endif
