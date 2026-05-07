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

static __m128i sl_sse2_loadu_128(const unsigned char* ptr)
{
    return _mm_loadu_si128((const __m128i*)ptr);
}

static size_t sl_sse2_first_mask_index(int mask)
{
    int offset = 0;

    for (offset = 0; offset < 16; offset += 1) {
        if ((mask & (1 << offset)) != 0) {
            return (size_t)offset;
        }
    }

    return 16U;
}

SlStatus sl_bytes_find_sse2(SlBytes bytes, unsigned char needle, SlBytesFindResult* out)
{
    const __m128i needle_vec = _mm_set1_epi8((char)needle);
    size_t index = 0U;
    SlBytesFindResult result = {.found = false, .index = bytes.length, .value = 0U};

    while (index + 16U <= bytes.length) {
        const __m128i chunk = sl_sse2_loadu_128(bytes.ptr + index);
        const int match_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle_vec));

        if (match_mask != 0) {
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

    while (index + 16U <= bytes.length) {
        size_t needle_index = 0U;
        int match_mask = 0;
        const __m128i chunk = sl_sse2_loadu_128(bytes.ptr + index);

        for (needle_index = 0U; needle_index < needles.length; needle_index += 1U) {
            const __m128i needle = _mm_set1_epi8((char)needles.ptr[needle_index]);
            match_mask |= _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle));
        }

        if (match_mask != 0) {
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
