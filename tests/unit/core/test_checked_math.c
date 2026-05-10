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

static int test_checked_add_helpers(void)
{
    size_t out = 99U;

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

    if (expect_status(sl_checked_add3_size(1U, 2U, 39U, &out), SL_STATUS_OK) != 0 || out != 42U) {
        return 13;
    }

    out = 4321U;
    if (expect_status(sl_checked_add3_size(SIZE_MAX - 1U, 1U, 1U, &out), SL_STATUS_OVERFLOW) != 0 ||
        out != 4321U)
    {
        return 14;
    }

    if (expect_status(sl_checked_add3_size(1U, 2U, 3U, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 15;
    }

    return 0;
}

static int test_checked_mul_helpers(void)
{
    size_t out = 99U;
    size_t half_plus_one = (SIZE_MAX / 2U) + 1U;
    size_t array_count = (SIZE_MAX / sizeof(uint64_t)) + 1U;

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

static int test_checked_array_helpers(void)
{
    size_t out = 99U;
    size_t array_count = (SIZE_MAX / sizeof(uint64_t)) + 1U;

    if (expect_status(sl_checked_array_size(3U, sizeof(uint64_t), &out), SL_STATUS_OK) != 0 ||
        out != 3U * sizeof(uint64_t))
    {
        return 16;
    }

    if (expect_status(sl_checked_array_size(0U, sizeof(uint64_t), &out), SL_STATUS_OK) != 0 ||
        out != 0U)
    {
        return 17;
    }

    out = 8765U;
    if (expect_status(sl_checked_array_size(array_count, sizeof(uint64_t), &out),
                      SL_STATUS_OVERFLOW) != 0 ||
        out != 8765U)
    {
        return 18;
    }

    if (expect_status(sl_checked_array_size(1U, sizeof(uint64_t), NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 19;
    }

    return 0;
}

static int test_boundary_matrix_for_add3_and_mul(void)
{
    size_t out = 0U;
    size_t sentinel = 4242U;
    size_t exact_half = SIZE_MAX / 2U;
    size_t exact_third = SIZE_MAX / 3U;

    if (expect_status(sl_checked_add3_size(0U, 0U, SIZE_MAX, &out), SL_STATUS_OK) != 0 ||
        out != SIZE_MAX ||
        expect_status(sl_checked_add3_size(SIZE_MAX - 2U, 1U, 1U, &out), SL_STATUS_OK) != 0 ||
        out != SIZE_MAX ||
        expect_status(sl_checked_add3_size(SIZE_MAX - 1U, 0U, 1U, &out), SL_STATUS_OK) != 0 ||
        out != SIZE_MAX)
    {
        return 23;
    }

    out = sentinel;
    if (expect_status(sl_checked_add3_size(SIZE_MAX, 1U, 0U, &out), SL_STATUS_OVERFLOW) != 0 ||
        out != sentinel)
    {
        return 24;
    }

    out = sentinel;
    if (expect_status(sl_checked_add3_size(SIZE_MAX - 1U, 1U, 1U, &out), SL_STATUS_OVERFLOW) != 0 ||
        out != sentinel)
    {
        return 25;
    }

    if (expect_status(sl_checked_mul_size(exact_half, 2U, &out), SL_STATUS_OK) != 0 ||
        out != exact_half * 2U ||
        expect_status(sl_checked_mul_size(exact_third, 3U, &out), SL_STATUS_OK) != 0 ||
        out != exact_third * 3U ||
        expect_status(sl_checked_mul_size(1U, SIZE_MAX, &out), SL_STATUS_OK) != 0 ||
        out != SIZE_MAX)
    {
        return 26;
    }

    out = sentinel;
    if (expect_status(sl_checked_mul_size((SIZE_MAX / 2U) + 1U, 2U, &out), SL_STATUS_OVERFLOW) !=
            0 ||
        out != sentinel)
    {
        return 27;
    }

    out = sentinel;
    if (expect_status(sl_checked_mul_size((SIZE_MAX / 3U) + 1U, 3U, &out), SL_STATUS_OVERFLOW) !=
            0 ||
        out != sentinel)
    {
        return 28;
    }

    return 0;
}

static int test_deterministic_checked_size_properties(void)
{
    size_t a = 0U;

    for (a = 0U; a < 128U; a += 1U) {
        size_t b = 0U;

        for (b = 0U; b < 128U; b += 1U) {
            size_t sum = 999U;
            size_t product = 999U;

            if (expect_status(sl_checked_add_size(a, b, &sum), SL_STATUS_OK) != 0 || sum != a + b) {
                return 20;
            }

            if (expect_status(sl_checked_mul_size(a, b, &product), SL_STATUS_OK) != 0 ||
                product != a * b)
            {
                return 21;
            }
        }
    }

    for (a = 2U; a < 128U; a += 1U) {
        size_t out = 2468U;
        size_t overflow_count = (SIZE_MAX / a) + 1U;

        if (expect_status(sl_checked_array_size(overflow_count, a, &out), SL_STATUS_OVERFLOW) !=
                0 ||
            out != 2468U)
        {
            return 22;
        }
    }

    return 0;
}

int main(void)
{
    int result = test_checked_add_helpers();
    if (result != 0) {
        return result;
    }

    result = test_checked_mul_helpers();
    if (result != 0) {
        return result;
    }

    result = test_checked_array_helpers();
    if (result != 0) {
        return result;
    }

    result = test_boundary_matrix_for_add3_and_mul();
    if (result != 0) {
        return result;
    }

    return test_deterministic_checked_size_properties();
}
