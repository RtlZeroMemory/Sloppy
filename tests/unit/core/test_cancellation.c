#include "sloppy/cancellation.h"

#include <stdbool.h>

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
    SlCancellationToken token = {0};

    sl_cancellation_token_init(&token);
    if (expect_true(!sl_cancellation_token_is_cancelled(&token)) != 0 ||
        sl_cancellation_token_reason(&token) != SL_CANCELLATION_REASON_NONE)
    {
        return 1;
    }

    if (expect_status(sl_cancellation_token_cancel(&token, SL_CANCELLATION_REASON_DEADLINE_EXCEEDED,
                                                   sl_str_from_cstr("handler deadline elapsed")),
                      SL_STATUS_OK) != 0)
    {
        return 2;
    }

    if (expect_true(sl_cancellation_token_is_cancelled(&token)) != 0 ||
        sl_cancellation_token_reason(&token) != SL_CANCELLATION_REASON_DEADLINE_EXCEEDED ||
        !sl_str_equal(token.detail, sl_str_from_cstr("handler deadline elapsed")))
    {
        return 3;
    }

    if (sl_cancellation_status_code(token.reason) != SL_STATUS_DEADLINE_EXCEEDED ||
        !sl_str_equal(sl_cancellation_reason_name(token.reason),
                      sl_str_from_cstr("deadline_exceeded")))
    {
        return 4;
    }

    if (expect_status(
            sl_cancellation_token_cancel(&token, SL_CANCELLATION_REASON_CANCELLED, sl_str_empty()),
            SL_STATUS_INVALID_STATE) != 0)
    {
        return 5;
    }

    if (expect_status(
            sl_cancellation_token_cancel(NULL, SL_CANCELLATION_REASON_CANCELLED, sl_str_empty()),
            SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(
            sl_cancellation_token_cancel(&token, SL_CANCELLATION_REASON_NONE, sl_str_empty()),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 6;
    }

    if (sl_cancellation_status_code(SL_CANCELLATION_REASON_BACKPRESSURE) !=
            SL_STATUS_CAPACITY_EXCEEDED ||
        sl_cancellation_status_code(SL_CANCELLATION_REASON_SHUTDOWN) != SL_STATUS_CANCELLED)
    {
        return 7;
    }

    return 0;
}
