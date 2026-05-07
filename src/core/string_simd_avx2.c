/*
 * src/core/string_simd_avx2.c
 *
 * Optional AVX2 string backend for configured advanced SIMD builds.
 */
#include "string_internal.h"

#if SL_STRING_SIMD_AVX2

#include <immintrin.h>
#include <stdint.h>

static __m256i sl_avx2_loadu_256(const char* ptr)
{
    return _mm256_loadu_si256((const __m256i*)ptr);
}

static bool sl_avx2_all_ascii_ci_equal(const char* left, const char* right, size_t length)
{
    const __m256i ascii_case_bit = _mm256_set1_epi8(0x20);
    const __m256i before_a = _mm256_set1_epi8((char)('a' - 1));
    const __m256i after_z = _mm256_set1_epi8((char)('z' + 1));
    size_t index = 0U;

    while (index + 32U <= length) {
        const __m256i left_chunk = sl_avx2_loadu_256(left + index);
        const __m256i right_chunk = sl_avx2_loadu_256(right + index);
        const __m256i direct_equal = _mm256_cmpeq_epi8(left_chunk, right_chunk);
        const __m256i left_lower = _mm256_or_si256(left_chunk, ascii_case_bit);
        const __m256i right_lower = _mm256_or_si256(right_chunk, ascii_case_bit);
        const __m256i lower_equal = _mm256_cmpeq_epi8(left_lower, right_lower);
        const __m256i left_alpha = _mm256_and_si256(_mm256_cmpgt_epi8(left_lower, before_a),
                                                    _mm256_cmpgt_epi8(after_z, left_lower));
        const __m256i right_alpha = _mm256_and_si256(_mm256_cmpgt_epi8(right_lower, before_a),
                                                     _mm256_cmpgt_epi8(after_z, right_lower));
        const __m256i case_equal =
            _mm256_and_si256(lower_equal, _mm256_and_si256(left_alpha, right_alpha));
        const __m256i equal = _mm256_or_si256(direct_equal, case_equal);

        if ((uint32_t)_mm256_movemask_epi8(equal) != UINT32_MAX) {
            return false;
        }

        index += 32U;
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

bool sl_str_equal_ci_ascii_avx2(SlStr left, SlStr right)
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

    return sl_avx2_all_ascii_ci_equal(left.ptr, right.ptr, left.length);
}

bool sl_str_contains_nul_avx2(SlStr str)
{
    const __m256i zero = _mm256_setzero_si256();
    size_t index = 0U;

    if (str.length == 0U) {
        return false;
    }

    while (index + 32U <= str.length) {
        const __m256i chunk = sl_avx2_loadu_256(str.ptr + index);
        const __m256i matches = _mm256_cmpeq_epi8(chunk, zero);

        if ((uint32_t)_mm256_movemask_epi8(matches) != 0U) {
            return true;
        }

        index += 32U;
    }

    for (; index < str.length; index += 1U) {
        if (str.ptr[index] == '\0') {
            return true;
        }
    }

    return false;
}

#endif
