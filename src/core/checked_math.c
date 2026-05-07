/*
 * src/core/checked_math.c
 *
 * Implements checked size_t arithmetic for future allocation and slice calculations.
 *
 * Safety invariants:
 * - NULL output pointers are rejected with status, not asserted;
 * - overflow is detected before performing wrapping arithmetic;
 * - failed calls leave caller-owned output storage untouched.
 *
 * Tests: tests/unit/core/test_checked_math.c.
 */
#include "sloppy/checked_math.h"

#include <stdint.h>

SlStatus sl_checked_add_size(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (a > SIZE_MAX - b) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    *out = a + b;
    return sl_status_ok();
}

SlStatus sl_checked_mul_size(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (a != 0U && b > SIZE_MAX / a) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    *out = a * b;
    return sl_status_ok();
}

SlStatus sl_checked_add3_size(size_t a, size_t b, size_t c, size_t* out)
{
    size_t partial = 0U;
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(a, b, &partial);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_checked_add_size(partial, c, out);
}

SlStatus sl_checked_array_size(size_t count, size_t elem_size, size_t* out)
{
    return sl_checked_mul_size(count, elem_size, out);
}
