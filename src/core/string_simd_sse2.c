/*
 * src/core/string_simd_sse2.c
 *
 * Optional SSE2 string backend for length-based SlStr primitives. ASCII
 * case-insensitive comparison keeps non-ASCII bytes exact.
 */
#include "string_internal.h"

#if SL_STRING_SIMD_SSE2

#include <emmintrin.h>

static __m128i sl_sse2_loadu_128(const char* ptr)
{
    return _mm_loadu_si128((const __m128i*)ptr);
}

static bool sl_sse2_all_ascii_ci_equal(const char* left, const char* right, size_t length)
{
    const __m128i ascii_case_bit = _mm_set1_epi8(0x20);
    const __m128i before_a = _mm_set1_epi8((char)('a' - 1));
    const __m128i after_z = _mm_set1_epi8((char)('z' + 1));
    size_t index = 0U;

    while (index + 16U <= length) {
        const __m128i left_chunk = sl_sse2_loadu_128(left + index);
        const __m128i right_chunk = sl_sse2_loadu_128(right + index);
        const __m128i direct_equal = _mm_cmpeq_epi8(left_chunk, right_chunk);
        const __m128i left_lower = _mm_or_si128(left_chunk, ascii_case_bit);
        const __m128i right_lower = _mm_or_si128(right_chunk, ascii_case_bit);
        const __m128i lower_equal = _mm_cmpeq_epi8(left_lower, right_lower);
        const __m128i left_alpha = _mm_and_si128(_mm_cmpgt_epi8(left_lower, before_a),
                                                 _mm_cmpgt_epi8(after_z, left_lower));
        const __m128i right_alpha = _mm_and_si128(_mm_cmpgt_epi8(right_lower, before_a),
                                                  _mm_cmpgt_epi8(after_z, right_lower));
        const __m128i case_equal =
            _mm_and_si128(lower_equal, _mm_and_si128(left_alpha, right_alpha));
        const __m128i equal = _mm_or_si128(direct_equal, case_equal);

        if (_mm_movemask_epi8(equal) != 0xffff) {
            return false;
        }

        index += 16U;
    }

    for (; index < length; index += 1U) {
        unsigned char left_ch = (unsigned char)left[index];
        unsigned char right_ch = (unsigned char)right[index];

        if (left_ch >= (unsigned char)'A' && left_ch <= (unsigned char)'Z') {
            left_ch = (unsigned char)(left_ch - (unsigned char)'A' + (unsigned char)'a');
        }
        if (right_ch >= (unsigned char)'A' && right_ch <= (unsigned char)'Z') {
            right_ch = (unsigned char)(right_ch - (unsigned char)'A' + (unsigned char)'a');
        }
        if (left_ch != right_ch) {
            return false;
        }
    }

    return true;
}

bool sl_str_equal_ci_ascii_sse2(SlStr left, SlStr right)
{
    if (left.length != right.length) {
        return false;
    }
    if (left.length == 0U) {
        return true;
    }
    if (left.ptr == NULL || right.ptr == NULL) {
        return false;
    }
    if (left.ptr == right.ptr) {
        return true;
    }

    return sl_sse2_all_ascii_ci_equal(left.ptr, right.ptr, left.length);
}

bool sl_str_contains_nul_sse2(SlStr str)
{
    const __m128i zero = _mm_setzero_si128();
    size_t index = 0U;

    if (str.length == 0U || str.ptr == NULL) {
        return false;
    }

    while (index + 16U <= str.length) {
        const __m128i chunk = sl_sse2_loadu_128(str.ptr + index);
        const __m128i matches = _mm_cmpeq_epi8(chunk, zero);

        if (_mm_movemask_epi8(matches) != 0) {
            return true;
        }

        index += 16U;
    }

    for (; index < str.length; index += 1U) {
        if (str.ptr[index] == '\0') {
            return true;
        }
    }

    return false;
}

#endif
