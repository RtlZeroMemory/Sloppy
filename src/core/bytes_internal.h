#ifndef SLOPPY_CORE_BYTES_INTERNAL_H
#define SLOPPY_CORE_BYTES_INTERNAL_H

#include "sloppy/bytes.h"

#ifndef SL_BYTES_SIMD_SSE2
#define SL_BYTES_SIMD_SSE2 0
#endif
#ifndef SL_BYTES_SIMD_AVX2
#define SL_BYTES_SIMD_AVX2 0
#endif

SlStatus sl_bytes_find_any_scalar(SlBytes bytes, SlBytes needles, SlBytesFindResult* out);
SlStatus sl_bytes_find_scalar(SlBytes bytes, unsigned char needle, SlBytesFindResult* out);

#if SL_BYTES_SIMD_SSE2
SlStatus sl_bytes_find_sse2(SlBytes bytes, unsigned char needle, SlBytesFindResult* out);
SlStatus sl_bytes_find_any_sse2(SlBytes bytes, SlBytes needles, SlBytesFindResult* out);
#endif

#if SL_BYTES_SIMD_AVX2
SlStatus sl_bytes_find_avx2(SlBytes bytes, unsigned char needle, SlBytesFindResult* out);
SlStatus sl_bytes_find_any_avx2(SlBytes bytes, SlBytes needles, SlBytesFindResult* out);
#endif

#endif
