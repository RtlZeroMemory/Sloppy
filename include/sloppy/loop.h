#ifndef SLOPPY_LOOP_H
#define SLOPPY_LOOP_H

#include "sloppy/container.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlLoop SlLoop;

typedef enum SlCompletionKind
{
    SL_COMPLETION_KIND_NONE = 0,
    SL_COMPLETION_KIND_TEST = 1,
    SL_COMPLETION_KIND_USER = 2,
    SL_COMPLETION_KIND_ASYNC = 3
} SlCompletionKind;

/*
 * Completion callback invoked synchronously by sl_loop_run_once() or sl_loop_drain().
 *
 * `loop` is the loop currently dispatching the completion. `payload` and `user` are
 * borrowed opaque pointers supplied to sl_loop_post() and may be NULL. The callback runs on
 * the caller thread; this loop creates no threads and does not support cross-thread
 * posting. Returning failure propagates that SlStatus to the caller, and the consumed
 * completion is not retried automatically.
 */
typedef SlStatus (*SlCompletionFn)(SlLoop* loop, SlCompletionKind kind, void* payload, void* user);

typedef struct SlCompletion
{
    SlCompletionKind kind;
    SlCompletionFn fn;
    void* payload;
    void* user;
} SlCompletion;

/*
 * SlLoop is Sloppy's caller-backed native completion queue.
 *
 * Storage is caller-owned: SlLoop never allocates, frees, calls OS APIs, or owns callback
 * payloads. The queue is fixed-capacity FIFO. Completion callbacks are invoked
 * synchronously on the caller thread and may post more completions if capacity permits.
 *
 * Threading and engine boundary:
 * - this queue is single-threaded and not thread-safe;
 * - cross-thread posting is outside the current loop contract;
 * - owner-thread enforcement remains an engine boundary responsibility;
 * - V8 isolates must only be entered by their owning JS event-loop thread;
 * - native worker threads must post future completions back instead of entering V8.
 */
struct SlLoop
{
    SlRingQueue queue;
    bool stopped;
    bool draining;
};

/*
 * Initializes `loop` over caller-owned completion storage.
 *
 * `loop` is required. `storage` is required when `capacity` is nonzero. Zero-capacity loops
 * are valid and can run/drain empty, but posting returns SL_STATUS_CAPACITY_EXCEEDED.
 */
SlStatus sl_loop_init(SlLoop* loop, SlCompletion* storage, size_t capacity);

/*
 * Clears pending completions and marks the loop running again.
 *
 * Reset does not invoke callbacks or free payloads. Passing NULL is a no-op.
 */
void sl_loop_reset(SlLoop* loop);

/*
 * Posts one completion to the FIFO queue.
 *
 * `loop` and `fn` are required. `payload` and `user` may be NULL. Posting after
 * sl_loop_stop() returns SL_STATUS_INVALID_STATE. Posting to a full queue returns
 * SL_STATUS_CAPACITY_EXCEEDED and does not mutate the queue.
 */
SlStatus sl_loop_post(SlLoop* loop, SlCompletionKind kind, SlCompletionFn fn, void* payload,
                      void* user);

/*
 * Runs at most one queued completion.
 *
 * The completion is consumed before its callback runs. `out_ran` is optional and receives 1
 * when a callback ran, otherwise 0. A stopped or empty loop succeeds with 0 callbacks run.
 */
SlStatus sl_loop_run_once(SlLoop* loop, size_t* out_ran);

/*
 * Runs queued completions until the queue is empty, the loop is stopped, or a callback
 * fails.
 *
 * `out_ran` is optional and receives the number of callbacks invoked by this drain call.
 * If a callback calls sl_loop_stop(), draining stops after that callback returns and leaves
 * remaining queued completions pending. If a callback returns failure, the failure is
 * returned and that completion is not retried. Nested drains are rejected with
 * SL_STATUS_INVALID_STATE.
 */
SlStatus sl_loop_drain(SlLoop* loop, size_t* out_ran);

/*
 * Stops the loop.
 *
 * Stop prevents future posts and prevents drain from invoking further callbacks after the
 * current callback returns. Passing NULL is a no-op.
 */
void sl_loop_stop(SlLoop* loop);

size_t sl_loop_pending_count(const SlLoop* loop);
size_t sl_loop_capacity(const SlLoop* loop);
bool sl_loop_is_stopped(const SlLoop* loop);

#ifdef __cplusplus
}
#endif

#endif
