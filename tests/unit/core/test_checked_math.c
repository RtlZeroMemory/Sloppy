#include "sloppy/checked_math.h"

#include <stdbool.h>
#include <stdint.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

int main(void)
{
    size_t out = 99U;
    size_t half_plus_one = (SIZE_MAX / 2U) + 1U;
    size_t array_count = (SIZE_MAX / sizeof(uint64_t)) + 1U;

    if (expect_status(sl_checked_add_size(0U, 0U, &out), SL_STATUS_OK) != 0 || out != 0U) {
        return 1;
    }

    if (expect_status(sl_checked_add_size(11U, 31U, &out), SL_STATUS_OK) != 0 || out != 42U) {
        return 2;
    }

    out = 77U;
    if (expect_status(sl_checked_add_size(SIZE_MAX, 1U, &out), SL_STATUS_OVERFLOW) != 0 ||
        out != 77U)
    {
        return 3;
    }

    if (expect_status(sl_checked_add_size(SIZE_MAX, 0U, &out), SL_STATUS_OK) != 0 ||
        out != SIZE_MAX)
    {
        return 4;
    }

    if (expect_status(sl_checked_add_size(1U, 2U, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 5;
    }

    if (expect_status(sl_checked_mul_size(6U, 7U, &out), SL_STATUS_OK) != 0 || out != 42U) {
        return 6;
    }

    if (expect_status(sl_checked_mul_size(SIZE_MAX, 0U, &out), SL_STATUS_OK) != 0 || out != 0U) {
        return 7;
    }

    out = 77U;
    if (expect_status(sl_checked_mul_size(SIZE_MAX, 2U, &out), SL_STATUS_OVERFLOW) != 0 ||
        out != 77U)
    {
        return 8;
    }

    if (expect_status(sl_checked_mul_size(half_plus_one, 2U, &out), SL_STATUS_OVERFLOW) != 0 ||
        out != 77U)
    {
        return 9;
    }

    if (expect_status(sl_checked_mul_size(1U, 2U, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 10;
    }

    out = 1234U;
    if (expect_status(sl_checked_add_size(SIZE_MAX, 1U, &out), SL_STATUS_OVERFLOW) != 0 ||
        out != 1234U)
    {
        return 11;
    }

    out = 5678U;
    if (expect_status(sl_checked_mul_size(array_count, sizeof(uint64_t), &out),
                      SL_STATUS_OVERFLOW) != 0 ||
        out != 5678U)
    {
        return 12;
    }

    return 0;
}
