#ifndef SLOPPY_CORE_STRING_INTERNAL_H
#define SLOPPY_CORE_STRING_INTERNAL_H

#include "sloppy/string.h"

#ifndef SL_STRING_SIMD_SSE2
#define SL_STRING_SIMD_SSE2 0
#endif
#ifndef SL_STRING_SIMD_AVX2
#define SL_STRING_SIMD_AVX2 0
#endif

bool sl_str_equal_ci_ascii_scalar(SlStr left, SlStr right);
bool sl_str_contains_nul_scalar(SlStr str);

#if SL_STRING_SIMD_SSE2
bool sl_str_equal_ci_ascii_sse2(SlStr left, SlStr right);
bool sl_str_contains_nul_sse2(SlStr str);
#endif

#if SL_STRING_SIMD_AVX2
bool sl_str_equal_ci_ascii_avx2(SlStr left, SlStr right);
bool sl_str_contains_nul_avx2(SlStr str);
#endif

#endif
