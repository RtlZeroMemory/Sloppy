/*
 * src/core/async.c
 *
 * Implements Sloppy's caller-owned native async settlement primitive.
 *
 * Safety invariants:
 * - async objects are caller-owned and never allocate or free memory;
 * - payload, user, and diagnostic pointers are borrowed until loop dispatch;
 * - settlement posts through SlLoop and never invokes continuations inline;
 * - state changes only after the loop post succeeds;
 * - reinitialization is rejected while a queued completion is still pending;
 * - each async object settles at most once;
 * - this primitive is single-threaded and has no V8, microtask, HTTP, or worker behavior.
 *
 * Tests: tests/unit/core/test_async.c.
 */
#include "sloppy/async.h"

static SlStatus sl_async_dispatch(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    SlAsync* async = (SlAsync*)payload;
    SlAsyncContinuationFn continuation = NULL;
    void* continuation_user = NULL;
    SlAsyncState state = SL_ASYNC_STATE_PENDING;
    SlAsyncResult result;

    (void)kind;
    (void)user;
    if (async == NULL || async->continuation == NULL || !async->has_result ||
        !async->completion_posted)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    continuation = async->continuation;
    continuation_user = async->continuation_user;
    state = async->state;
    result = async->result;
    async->completion_posted = false;

    return continuation(loop, state, &result, continuation_user);
}

static SlStatus sl_async_settle(SlAsync* async, SlLoop* loop, SlAsyncState state, SlStatus status,
                                const SlDiag* diag, void* payload, void* user)
{
    SlStatus post_status;
    SlAsyncResult result;

    if (async == NULL || loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (state != SL_ASYNC_STATE_FULFILLED && sl_status_is_ok(status)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (async->state != SL_ASYNC_STATE_PENDING || async->completion_posted) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    result.status = status;
    result.diag = diag;
    result.payload = payload;
    result.user = user;

    post_status = sl_loop_post(loop, SL_COMPLETION_KIND_ASYNC, sl_async_dispatch, async, async);
    if (!sl_status_is_ok(post_status)) {
        return post_status;
    }

    async->state = state;
    async->result = result;
    async->has_result = true;
    async->completion_posted = true;

    return sl_status_ok();
}

SlStatus sl_async_init(SlAsync* async, SlAsyncContinuationFn continuation, void* continuation_user)
{
    if (async == NULL || continuation == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (async->completion_posted) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    async->state = SL_ASYNC_STATE_PENDING;
    async->continuation = continuation;
    async->continuation_user = continuation_user;
    async->result.status = sl_status_ok();
    async->result.diag = NULL;
    async->result.payload = NULL;
    async->result.user = NULL;
    async->has_result = false;
    async->completion_posted = false;

    return sl_status_ok();
}

SlAsyncState sl_async_state(const SlAsync* async)
{
    return async == NULL ? SL_ASYNC_STATE_PENDING : async->state;
}

bool sl_async_is_pending(const SlAsync* async)
{
    return async != NULL && async->state == SL_ASYNC_STATE_PENDING;
}

bool sl_async_is_settled(const SlAsync* async)
{
    return async != NULL && async->state != SL_ASYNC_STATE_PENDING;
}

SlStatus sl_async_fulfill(SlAsync* async, SlLoop* loop, void* payload, void* user)
{
    return sl_async_settle(async, loop, SL_ASYNC_STATE_FULFILLED, sl_status_ok(), NULL, payload,
                           user);
}

SlStatus sl_async_reject(SlAsync* async, SlLoop* loop, SlStatus status, const SlDiag* diag)
{
    return sl_async_settle(async, loop, SL_ASYNC_STATE_REJECTED, status, diag, NULL, NULL);
}

SlStatus sl_async_cancel(SlAsync* async, SlLoop* loop, SlStatus status, const SlDiag* diag)
{
    return sl_async_settle(async, loop, SL_ASYNC_STATE_CANCELLED, status, diag, NULL, NULL);
}
