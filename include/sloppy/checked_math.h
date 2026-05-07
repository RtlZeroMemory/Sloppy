#ifndef SLOPPY_CHECKED_MATH_H
#define SLOPPY_CHECKED_MATH_H

#include "sloppy/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Checked size arithmetic for allocation and slice-size calculations.
 *
 * `out` is required. On success, `*out` receives the computed value. On invalid argument or
 * overflow, `*out` is left unchanged and the returned SlStatus explains the failure class.
 */
SlStatus sl_checked_add_size(size_t a, size_t b, size_t* out);
SlStatus sl_checked_mul_size(size_t a, size_t b, size_t* out);
SlStatus sl_checked_add3_size(size_t a, size_t b, size_t c, size_t* out);
SlStatus sl_checked_array_size(size_t count, size_t elem_size, size_t* out);

#ifdef __cplusplus
}
#endif

#endif
