/*
 * src/core/cancellation.c
 *
 * Implements Sloppy's tiny request cancellation/deadline token. The token is intentionally
 * caller-owned and allocation-free so runtime layers can pass cancellation state through
 * async and cleanup boundaries before a real scheduler exists.
 */
#include "sloppy/cancellation.h"

static bool sl_cancellation_detail_valid(SlStr detail)
{
    return detail.length == 0U || detail.ptr != NULL;
}

void sl_cancellation_token_init(SlCancellationToken* token)
{
    if (token == NULL) {
        return;
    }

    token->cancelled = false;
    token->reason = SL_CANCELLATION_REASON_NONE;
    token->detail = sl_str_empty();
}

SlStatus sl_cancellation_token_cancel(SlCancellationToken* token, SlCancellationReason reason,
                                      SlStr detail)
{
    if (token == NULL || reason == SL_CANCELLATION_REASON_NONE ||
        !sl_cancellation_detail_valid(detail))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (token->cancelled) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    token->cancelled = true;
    token->reason = reason;
    token->detail = detail;
    return sl_status_ok();
}

bool sl_cancellation_token_is_cancelled(const SlCancellationToken* token)
{
    return token != NULL && token->cancelled;
}

SlCancellationReason sl_cancellation_token_reason(const SlCancellationToken* token)
{
    if (token == NULL || !token->cancelled) {
        return SL_CANCELLATION_REASON_NONE;
    }

    return token->reason;
}

SlStr sl_cancellation_reason_name(SlCancellationReason reason)
{
    switch (reason) {
    case SL_CANCELLATION_REASON_CANCELLED:
        return sl_str_from_cstr("cancelled");
    case SL_CANCELLATION_REASON_DEADLINE_EXCEEDED:
        return sl_str_from_cstr("deadline_exceeded");
    case SL_CANCELLATION_REASON_BACKPRESSURE:
        return sl_str_from_cstr("backpressure");
    case SL_CANCELLATION_REASON_SHUTDOWN:
        return sl_str_from_cstr("shutdown");
    case SL_CANCELLATION_REASON_NONE:
    default:
        return sl_str_from_cstr("none");
    }
}

SlStatusCode sl_cancellation_status_code(SlCancellationReason reason)
{
    switch (reason) {
    case SL_CANCELLATION_REASON_DEADLINE_EXCEEDED:
        return SL_STATUS_DEADLINE_EXCEEDED;
    case SL_CANCELLATION_REASON_BACKPRESSURE:
        return SL_STATUS_CAPACITY_EXCEEDED;
    case SL_CANCELLATION_REASON_CANCELLED:
    case SL_CANCELLATION_REASON_SHUTDOWN:
        return SL_STATUS_CANCELLED;
    case SL_CANCELLATION_REASON_NONE:
    default:
        return SL_STATUS_INVALID_ARGUMENT;
    }
}
