#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int test_status_round_trip_and_enum_completeness(void)
{
    const SlStatusCode codes[] = {
        SL_STATUS_OK,
        SL_STATUS_INVALID_ARGUMENT,
        SL_STATUS_OUT_OF_MEMORY,
        SL_STATUS_OVERFLOW,
        SL_STATUS_OUT_OF_RANGE,
        SL_STATUS_ASSERTION_FAILED,
        SL_STATUS_INTERNAL,
        SL_STATUS_UNSUPPORTED,
        SL_STATUS_INVALID_STATE,
        SL_STATUS_CAPACITY_EXCEEDED,
        SL_STATUS_STALE_RESOURCE,
        SL_STATUS_WRONG_RESOURCE_KIND,
        SL_STATUS_CANCELLED,
        SL_STATUS_DEADLINE_EXCEEDED,
    };
    size_t index = 0U;
    size_t other = 0U;

    for (index = 0U; index < sizeof(codes) / sizeof(codes[0]); index += 1U) {
        SlStatus status = sl_status_from_code(codes[index]);
        if (expect_true(sl_status_code(status) == codes[index]) != 0) {
            return 1;
        }
        if (expect_true(sl_status_is_ok(status) == (codes[index] == SL_STATUS_OK)) != 0) {
            return 2;
        }
        for (other = index + 1U; other < sizeof(codes) / sizeof(codes[0]); other += 1U) {
            if (expect_true(codes[index] != codes[other]) != 0) {
                return 3;
            }
        }
    }

    if (expect_true(SL_STATUS_OK == 0) != 0 || expect_true(SL_STATUS_DEADLINE_EXCEEDED == 13) != 0)
    {
        return 4;
    }

    return 0;
}

int main(void)
{
    SlStatus ok = sl_status_ok();
    SlStatus invalid = sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    SlStatus overflow = sl_status_from_code(SL_STATUS_OVERFLOW);
    SlStatus invalid_state = sl_status_from_code(SL_STATUS_INVALID_STATE);
    SlStatus capacity = sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    SlStatus stale = sl_status_from_code(SL_STATUS_STALE_RESOURCE);
    SlStatus wrong_kind = sl_status_from_code(SL_STATUS_WRONG_RESOURCE_KIND);
    SlStatus cancelled = sl_status_from_code(SL_STATUS_CANCELLED);
    SlStatus deadline = sl_status_from_code(SL_STATUS_DEADLINE_EXCEEDED);
    SlStatus oom = sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    SlStatus out_of_range = sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    SlStatus assertion_failed = sl_status_from_code(SL_STATUS_ASSERTION_FAILED);
    SlStatus internal = sl_status_from_code(SL_STATUS_INTERNAL);
    SlStatus unsupported = sl_status_from_code(SL_STATUS_UNSUPPORTED);

    if (expect_true(sl_status_is_ok(ok)) != 0) {
        return 10;
    }

    if (expect_true(sl_status_code(ok) == SL_STATUS_OK) != 0) {
        return 11;
    }

    if (expect_true(!sl_status_is_ok(invalid)) != 0) {
        return 12;
    }

    if (expect_true(sl_status_code(invalid) == SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 13;
    }

    if (expect_true(sl_status_code(oom) == SL_STATUS_OUT_OF_MEMORY) != 0) {
        return 14;
    }

    if (expect_true(sl_status_code(overflow) == SL_STATUS_OVERFLOW) != 0) {
        return 15;
    }

    if (expect_true(sl_status_code(out_of_range) == SL_STATUS_OUT_OF_RANGE) != 0) {
        return 16;
    }

    if (expect_true(sl_status_code(assertion_failed) == SL_STATUS_ASSERTION_FAILED) != 0) {
        return 17;
    }

    if (expect_true(sl_status_code(internal) == SL_STATUS_INTERNAL) != 0) {
        return 18;
    }

    if (expect_true(sl_status_code(unsupported) == SL_STATUS_UNSUPPORTED) != 0) {
        return 19;
    }

    if (expect_true(sl_status_code(invalid_state) == SL_STATUS_INVALID_STATE) != 0) {
        return 20;
    }

    if (expect_true(sl_status_code(capacity) == SL_STATUS_CAPACITY_EXCEEDED) != 0) {
        return 21;
    }

    if (expect_true(sl_status_code(stale) == SL_STATUS_STALE_RESOURCE) != 0) {
        return 22;
    }

    if (expect_true(sl_status_code(wrong_kind) == SL_STATUS_WRONG_RESOURCE_KIND) != 0) {
        return 23;
    }

    if (expect_true(sl_status_code(cancelled) == SL_STATUS_CANCELLED) != 0) {
        return 24;
    }

    if (expect_true(sl_status_code(deadline) == SL_STATUS_DEADLINE_EXCEEDED) != 0) {
        return 25;
    }

    return test_status_round_trip_and_enum_completeness();
}
