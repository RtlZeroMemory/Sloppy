#ifndef SLOPPY_WORKER_POOL_H
#define SLOPPY_WORKER_POOL_H

#include "sloppy/loop.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY 8U

typedef struct SlWorkerPool SlWorkerPool;

typedef enum SlWorkerPoolMode
{
    SL_WORKER_POOL_MODE_NONE = 0,
    SL_WORKER_POOL_MODE_INLINE = 1
} SlWorkerPoolMode;

typedef enum SlWorkKind
{
    SL_WORK_KIND_NONE = 0,
    SL_WORK_KIND_TEST = 1,
    SL_WORK_KIND_BLOCKING_IO = 2,
    SL_WORK_KIND_CPU = 3
} SlWorkKind;

/*
 * Work callback for native work submitted to SlWorkerPool.
 *
 * `payload` and `user` are borrowed and may be NULL. `out_result` is required by the
 * worker-pool implementation and starts as NULL. When the callback stores a non-NULL result
 * pointer, ownership transfers to the completion callback after the completion is posted
 * and dispatched. If posting fails before dispatch, the worker pool calls
 * SlWorkResultDestroyFn when one was provided. This skeleton runs the callback inline on
 * the caller thread; future real worker threads must not enter V8 from this callback.
 */
typedef SlStatus (*SlWorkFn)(void* payload, void* user, void** out_result);

/*
 * Destroys an owned work result when the worker pool cannot post a completion.
 *
 * `result` is the pointer produced by SlWorkFn and `user` is SlWorkItem.user. The destroy
 * callback is never called after the completion callback has run because ownership has
 * transferred to that completion callback.
 */
typedef void (*SlWorkResultDestroyFn)(void* result, void* user);

typedef struct SlWorkItem
{
    SlWorkKind kind;
    SlWorkFn run;
    void* payload;
    void* user;
    SlWorkResultDestroyFn destroy_result;
} SlWorkItem;

/*
 * Completion callback posted back to SlLoop after work finishes.
 *
 * The callback runs synchronously on the thread calling sl_loop_run_once() or
 * sl_loop_drain(). `result` is owned by this completion callback when non-NULL, including
 * failure-status completions where the work callback deliberately returned a result.
 * `user` is the completion user supplied to sl_worker_pool_submit().
 */
typedef SlStatus (*SlWorkCompletionFn)(SlLoop* loop, SlWorkKind kind, SlStatus status, void* result,
                                       void* user);

typedef struct SlWorkerPoolCompletionRecord
{
    bool in_use;
    SlWorkKind kind;
    SlStatus status;
    void* result;
    SlWorkResultDestroyFn destroy_result;
    void* destroy_user;
    SlWorkCompletionFn completion;
    void* completion_user;
} SlWorkerPoolCompletionRecord;

/*
 * SlWorkerPool is the first worker-pool design skeleton.
 *
 * Only SL_WORKER_POOL_MODE_INLINE is implemented. It runs SlWorkFn immediately on the
 * caller thread and posts completion back to SlLoop for deterministic tests. The pool
 * stores a small fixed number of pending completion records and never allocates, starts
 * threads, uses atomics/locks, calls OS APIs, depends on libuv, or enters V8.
 */
struct SlWorkerPool
{
    SlWorkerPoolMode mode;
    SlWorkerPoolCompletionRecord completions[SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY];
};

/*
 * Initializes an inline worker pool.
 *
 * Storage must be zero-initialized before first use, or already initialized by
 * sl_worker_pool_init_inline(). Reinitialization while completions are pending is rejected
 * with SL_STATUS_INVALID_STATE so queued SlLoop callbacks cannot be orphaned or corrupted.
 */
SlStatus sl_worker_pool_init_inline(SlWorkerPool* pool);

/*
 * Destroys and clears pending inline worker completions.
 *
 * This is the supported cleanup path when a caller deliberately discards queued worker
 * completions from the owning SlLoop, for example by calling sl_loop_reset() before drain.
 * Call sl_loop_reset() first so the loop no longer holds callbacks pointing at these
 * records. Each pending non-NULL result is destroyed with its submitted destroy callback
 * when one exists. Passing NULL is a no-op.
 */
void sl_worker_pool_reset_inline(SlWorkerPool* pool);

/*
 * Submits one work item and posts its completion to `completion_loop`.
 *
 * `pool`, `completion_loop`, `item`, `item->run`, and `completion` are required.
 * `item->payload`, `item->user`, and `completion_user` may be NULL. `item->kind` must be a
 * supported non-NONE kind. In inline mode the work callback runs before this function
 * returns, but the completion callback is not invoked until the loop drains.
 *
 * If work succeeds or fails, the resulting status is posted to SlLoop. If the loop post
 * fails, any result produced by the work callback is destroyed through
 * `item->destroy_result`, when provided, before the post failure is returned.
 * Reinitializing a pool while worker completions are pending is rejected with
 * SL_STATUS_INVALID_STATE; drain the loop or discard loop completions and call
 * sl_worker_pool_reset_inline() first.
 */
SlStatus sl_worker_pool_submit(SlWorkerPool* pool, SlLoop* completion_loop, const SlWorkItem* item,
                               SlWorkCompletionFn completion, void* completion_user);

#ifdef __cplusplus
}
#endif

#endif
