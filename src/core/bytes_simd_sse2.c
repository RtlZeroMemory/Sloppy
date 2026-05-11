/*
 * src/core/bytes_simd_sse2.c
 *
 * Optional SSE2 byte-search backend. The public API remains scalar-first:
 * bytes.c validates the SlBytes contract and falls back to the scalar reference
 * whenever this backend is not compiled.
 */
#include "bytes_internal.h"

#if SL_BYTES_SIMD_SSE2

#include <emmintrin.h>
#include <stdint.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

static __m128i sl_sse2_loadu_128(const unsigned char* ptr)
{
    return _mm_loadu_si128((const __m128i*)ptr);
}

static size_t sl_sse2_first_mask_index(uint32_t mask)
{
    if (mask == 0U) {
        return 16U;
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

SlStatus sl_bytes_find_sse2(SlBytes bytes, unsigned char needle, SlBytesFindResult* out)
{
    const __m128i needle_vec = _mm_set1_epi8((char)needle);
    size_t index = 0U;
    SlBytesFindResult result = {.found = false, .index = bytes.length, .value = 0U};

    while (index + 16U <= bytes.length) {
        const __m128i chunk = sl_sse2_loadu_128(bytes.ptr + index);
        const uint32_t match_mask = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle_vec));

        if (match_mask != 0U) {
            result.found = true;
            result.index = index + sl_sse2_first_mask_index(match_mask);
            result.value = needle;
            *out = result;
            return sl_status_ok();
        }

        index += 16U;
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

SlStatus sl_bytes_find_any_sse2(SlBytes bytes, SlBytes needles, SlBytesFindResult* out)
{
    size_t index = 0U;
    SlBytesFindResult result = {.found = false, .index = bytes.length, .value = 0U};

    if (needles.length == 0U) {
        *out = result;
        return sl_status_ok();
    }
    if (needles.length == 1U) {
        return sl_bytes_find_sse2(bytes, needles.ptr[0], out);
    }
    if (needles.length <= 4U) {
        const __m128i needle0 = _mm_set1_epi8((char)needles.ptr[0]);
        const __m128i needle1 = _mm_set1_epi8((char)needles.ptr[1]);
        const __m128i needle2 =
            needles.length > 2U ? _mm_set1_epi8((char)needles.ptr[2]) : _mm_setzero_si128();
        const __m128i needle3 =
            needles.length > 3U ? _mm_set1_epi8((char)needles.ptr[3]) : _mm_setzero_si128();

        while (index + 16U <= bytes.length) {
            const __m128i chunk = sl_sse2_loadu_128(bytes.ptr + index);
            uint32_t match_mask = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle0)) |
                                  (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle1));

            if (needles.length > 2U) {
                match_mask |= (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle2));
            }
            if (needles.length > 3U) {
                match_mask |= (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle3));
            }

            if (match_mask != 0U) {
                size_t offset = sl_sse2_first_mask_index(match_mask);
                result.found = true;
                result.index = index + offset;
                result.value = bytes.ptr[result.index];
                *out = result;
                return sl_status_ok();
            }

            index += 16U;
        }
    }

    while (index + 16U <= bytes.length) {
        size_t needle_index = 0U;
        uint32_t match_mask = 0U;
        const __m128i chunk = sl_sse2_loadu_128(bytes.ptr + index);

        for (needle_index = 0U; needle_index < needles.length; needle_index += 1U) {
            const __m128i needle = _mm_set1_epi8((char)needles.ptr[needle_index]);
            match_mask |= (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle));
        }

        if (match_mask != 0U) {
            size_t offset = sl_sse2_first_mask_index(match_mask);
            result.found = true;
            result.index = index + offset;
            result.value = bytes.ptr[result.index];
            *out = result;
            return sl_status_ok();
        }

        index += 16U;
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
