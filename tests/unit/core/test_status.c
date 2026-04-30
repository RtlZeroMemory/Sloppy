#include "sloppy/status.h"

#include <stdbool.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
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

    if (expect_true(sl_status_is_ok(ok)) != 0) {
        return 1;
    }

    if (expect_true(sl_status_code(ok) == SL_STATUS_OK) != 0) {
        return 2;
    }

    if (expect_true(!sl_status_is_ok(invalid)) != 0) {
        return 3;
    }

    if (expect_true(sl_status_code(invalid) == SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 4;
    }

    if (expect_true(sl_status_code(overflow) == SL_STATUS_OVERFLOW) != 0) {
        return 5;
    }

    if (expect_true(sl_status_code(invalid_state) == SL_STATUS_INVALID_STATE) != 0) {
        return 6;
    }

    if (expect_true(sl_status_code(capacity) == SL_STATUS_CAPACITY_EXCEEDED) != 0) {
        return 7;
    }

    if (expect_true(sl_status_code(stale) == SL_STATUS_STALE_RESOURCE) != 0) {
        return 8;
    }

    if (expect_true(sl_status_code(wrong_kind) == SL_STATUS_WRONG_RESOURCE_KIND) != 0) {
        return 9;
    }

    if (expect_true(sl_status_code(cancelled) == SL_STATUS_CANCELLED) != 0) {
        return 10;
    }

    if (expect_true(sl_status_code(deadline) == SL_STATUS_DEADLINE_EXCEEDED) != 0) {
        return 11;
    }

    return 0;
}
