#ifndef SLOPPY_ASYNC_H
#define SLOPPY_ASYNC_H

#include "sloppy/diagnostics.h"
#include "sloppy/loop.h"
#include "sloppy/status.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlAsyncState
{
    SL_ASYNC_STATE_PENDING = 0,
    SL_ASYNC_STATE_FULFILLED = 1,
    SL_ASYNC_STATE_REJECTED = 2,
    SL_ASYNC_STATE_CANCELLED = 3
} SlAsyncState;

/*
 * Native async settlement result carried by SlAsync.
 *
 * `payload`, `user`, and `diag` are borrowed. SlAsync does not allocate, copy, free, or own
 * them. A borrowed diagnostic must outlive loop dispatch for this primitive; request
 * scope/V8 integrations must define stronger ownership for async diagnostics before use.
 */
typedef struct SlAsyncResult
{
    SlStatus status;
    const SlDiag* diag;
    void* payload;
    void* user;
} SlAsyncResult;

/*
 * Continuation invoked by SlLoop when a settled SlAsync completion is drained.
 *
 * The callback runs synchronously on the thread calling sl_loop_run_once() or
 * sl_loop_drain(). Returning failure propagates through the loop. SlAsync is not
 * thread-safe, and this primitive does not support cross-thread settlement or posting.
 */
typedef SlStatus (*SlAsyncContinuationFn)(SlLoop* loop, SlAsyncState state,
                                          const SlAsyncResult* result, void* user);

/*
 * Caller-owned native async settlement object.
 *
 * Storage passed to sl_async_init() must be zero-initialized before first use, or must
 * already have been initialized by sl_async_init(). SlAsync starts pending and may be
 * settled exactly once per initialization. Settlement posts one completion to SlLoop; the
 * continuation is not invoked inline. If posting fails, state remains pending and no result
 * is stored. A settled object must not be reinitialized while its queued completion is
 * still pending; sl_async_init() rejects that state. This object owns no memory and is not
 * thread-safe.
 */
typedef struct SlAsync
{
    SlAsyncState state;
    SlAsyncContinuationFn continuation;
    void* continuation_user;
    SlAsyncResult result;
    bool has_result;
    bool completion_posted;
} SlAsync;

SlStatus sl_async_init(SlAsync* async, SlAsyncContinuationFn continuation, void* continuation_user);

SlAsyncState sl_async_state(const SlAsync* async);
bool sl_async_is_pending(const SlAsync* async);
bool sl_async_is_settled(const SlAsync* async);

SlStatus sl_async_fulfill(SlAsync* async, SlLoop* loop, void* payload, void* user);
SlStatus sl_async_reject(SlAsync* async, SlLoop* loop, SlStatus status, const SlDiag* diag);
SlStatus sl_async_cancel(SlAsync* async, SlLoop* loop, SlStatus status, const SlDiag* diag);

#ifdef __cplusplus
}
#endif

#endif
