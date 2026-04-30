#ifndef SLOPPY_ASYNC_BACKEND_H
#define SLOPPY_ASYNC_BACKEND_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlAsyncLoop SlAsyncLoop;
typedef struct SlAsyncCompletion SlAsyncCompletion;

typedef enum SlAsyncBackendKind
{
    SL_ASYNC_BACKEND_NONE = 0,
    SL_ASYNC_BACKEND_LIBUV = 1,
    SL_ASYNC_BACKEND_TEST = 2
} SlAsyncBackendKind;

typedef enum SlAsyncCompletionKind
{
    SL_ASYNC_COMPLETION_NONE = 0,
    SL_ASYNC_COMPLETION_TEST = 1,
    SL_ASYNC_COMPLETION_NATIVE = 2,
    SL_ASYNC_COMPLETION_V8_CONTINUATION = 3
} SlAsyncCompletionKind;

typedef enum SlAsyncOperationKind
{
    SL_ASYNC_OPERATION_INTERNAL_COMPLETION = 0,
    SL_ASYNC_OPERATION_NONBLOCKING_IO = 1,
    SL_ASYNC_OPERATION_BLOCKING_OFFLOAD = 2,
    SL_ASYNC_OPERATION_TIMER = 3,
    SL_ASYNC_OPERATION_PROVIDER = 4
} SlAsyncOperationKind;

typedef SlStatus (*SlAsyncCompletionDispatchFn)(SlAsyncLoop* loop,
                                                const SlAsyncCompletion* completion, void* user);
typedef void (*SlAsyncCompletionCleanupFn)(const SlAsyncCompletion* completion, void* user);
typedef SlStatus (*SlAsyncScopeRetainFn)(void* scope, void* user);
typedef void (*SlAsyncScopeReleaseFn)(void* scope, void* user);

typedef struct SlAsyncScopeRef
{
    void* scope;
    SlAsyncScopeRetainFn retain;
    SlAsyncScopeReleaseFn release;
    void* user;
} SlAsyncScopeRef;

struct SlAsyncCompletion
{
    SlAsyncCompletionKind kind;
    SlAsyncOperationKind operation_kind;
    SlStatus status;
    const SlDiag* diag;
    void* payload;
    void* operation;
    SlAsyncScopeRef scope;
    SlAsyncCompletionDispatchFn dispatch;
    void* dispatch_user;
    SlAsyncCompletionCleanupFn cleanup;
    void* cleanup_user;
};

/*
 * Creates a Slop-owned async backend loop over caller-owned completion storage.
 *
 * The returned loop is arena-owned and must be disposed before the arena is reset. The
 * completion queue is fixed-capacity and bounded for every backend. Posting transfers
 * completion ownership only on success; failed posts leave cleanup to the caller. Queued
 * completion payloads must point to owned/stable native memory, not borrowed request views
 * that can disappear before owner-thread dispatch.
 */
SlStatus sl_async_loop_create(SlAsyncBackendKind kind, SlArena* arena, SlAsyncCompletion* storage,
                              size_t capacity, SlAsyncLoop** out_loop);

/*
 * Disposes a loop and discards pending completions.
 *
 * Pending owned completions run their cleanup callback and scope release exactly once.
 * Full cancellation, deadline, backpressure, and shutdown drain policy is deliberately
 * deferred to ENGINE-12.C.
 */
void sl_async_loop_dispose(SlAsyncLoop* loop);

/*
 * Posts one native completion.
 *
 * This API is safe to call from non-owner threads when the backend supports it. The libuv
 * backend uses a thread-safe wakeup internally; the deterministic test backend is intended
 * for single-threaded unit tests. Overflow is deterministic and returns
 * SL_STATUS_CAPACITY_EXCEEDED without taking ownership.
 */
SlStatus sl_async_loop_post(SlAsyncLoop* loop, const SlAsyncCompletion* completion);

/*
 * Dispatches completions on the owner thread.
 *
 * run_once dispatches at most one completion. drain dispatches until the queue is empty or
 * `max_count` completions have run; max_count == 0 means drain until idle. Dispatching from
 * a detectable non-owner thread returns SL_STATUS_INVALID_STATE before invoking callbacks.
 */
SlStatus sl_async_loop_run_once(SlAsyncLoop* loop, size_t* out_ran);
SlStatus sl_async_loop_drain(SlAsyncLoop* loop, size_t max_count, size_t* out_ran);

SlAsyncBackendKind sl_async_loop_backend_kind(const SlAsyncLoop* loop);
size_t sl_async_loop_pending_count(const SlAsyncLoop* loop);
size_t sl_async_loop_capacity(const SlAsyncLoop* loop);
bool sl_async_loop_is_owner_thread(const SlAsyncLoop* loop);
bool sl_async_loop_is_disposed(const SlAsyncLoop* loop);

#ifdef __cplusplus
}
#endif

#endif
