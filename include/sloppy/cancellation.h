#ifndef SLOPPY_CANCELLATION_H
#define SLOPPY_CANCELLATION_H

#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlCancellationReason
{
    SL_CANCELLATION_REASON_NONE = 0,
    SL_CANCELLATION_REASON_CANCELLED = 1,
    SL_CANCELLATION_REASON_DEADLINE_EXCEEDED = 2,
    SL_CANCELLATION_REASON_BACKPRESSURE = 3,
    SL_CANCELLATION_REASON_SHUTDOWN = 4
} SlCancellationReason;

/*
 * Small caller-owned cancellation token for request/async boundaries.
 *
 * The token stores borrowed detail text; callers must keep that text alive for as long as
 * consumers may inspect the token. This primitive does not allocate, start timers, or post
 * callbacks. Deadlines and backpressure reject work by cancelling through the same token so
 * request cleanup has one observable path.
 */
typedef struct SlCancellationToken
{
    bool cancelled;
    SlCancellationReason reason;
    SlStr detail;
} SlCancellationToken;

void sl_cancellation_token_init(SlCancellationToken* token);
SlStatus sl_cancellation_token_cancel(SlCancellationToken* token, SlCancellationReason reason,
                                      SlStr detail);
bool sl_cancellation_token_is_cancelled(const SlCancellationToken* token);
SlCancellationReason sl_cancellation_token_reason(const SlCancellationToken* token);
SlStr sl_cancellation_reason_name(SlCancellationReason reason);
SlStatusCode sl_cancellation_status_code(SlCancellationReason reason);
SlDiagCode sl_cancellation_diag_code(SlCancellationReason reason);

#ifdef __cplusplus
}
#endif

#endif
