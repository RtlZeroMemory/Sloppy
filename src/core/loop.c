/*
 * src/core/loop.c
 *
 * Implements Sloppy's first event-loop abstraction skeleton as a caller-backed native
 * completion queue.
 *
 * Safety invariants:
 * - completion storage is caller-owned;
 * - no raw allocation, platform API, libuv, V8, atomics, or locks are used;
 * - posts fail without mutating queue state when the loop is stopped or full;
 * - completions are consumed before callbacks run, so failed callbacks are not retried;
 * - this skeleton is single-threaded and rejects nested drain calls on the same loop.
 *
 * Tests: tests/unit/core/test_loop.c.
 */
#include "sloppy/loop.h"

static void sl_loop_clear_slots(SlLoop* loop)
{
    if (loop == NULL) {
        return;
    }

    sl_ring_queue_clear(&loop->queue);
}

SlStatus sl_loop_init(SlLoop* loop, SlCompletion* storage, size_t capacity)
{
    SlStatus status;

    if (loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (storage == NULL && capacity != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_ring_queue_init(&loop->queue, storage, sizeof(SlCompletion), capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    loop->stopped = false;
    loop->draining = false;
    return sl_status_ok();
}

void sl_loop_reset(SlLoop* loop)
{
    bool was_draining = false;

    if (loop == NULL) {
        return;
    }

    was_draining = loop->draining;
    sl_loop_clear_slots(loop);
    loop->stopped = false;
    loop->draining = was_draining;
}

SlStatus sl_loop_post(SlLoop* loop, SlCompletionKind kind, SlCompletionFn fn, void* payload,
                      void* user)
{
    SlCompletion completion;

    if (loop == NULL || fn == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->stopped) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    completion.kind = kind;
    completion.fn = fn;
    completion.payload = payload;
    completion.user = user;

    return sl_ring_queue_push(&loop->queue, &completion);
}

SlStatus sl_loop_run_once(SlLoop* loop, size_t* out_ran)
{
    SlCompletion completion;

    if (out_ran != NULL) {
        *out_ran = 0U;
    }

    if (loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->stopped || sl_ring_queue_is_empty(&loop->queue)) {
        return sl_status_ok();
    }

    if (!sl_ring_queue_pop_front(&loop->queue, &completion)) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (out_ran != NULL) {
        *out_ran = 1U;
    }

    return completion.fn(loop, completion.kind, completion.payload, completion.user);
}

SlStatus sl_loop_drain(SlLoop* loop, size_t* out_ran)
{
    SlStatus status;
    size_t ran = 0U;

    if (out_ran != NULL) {
        *out_ran = 0U;
    }

    if (loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->draining) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    loop->draining = true;
    status = sl_status_ok();

    while (!loop->stopped && !sl_ring_queue_is_empty(&loop->queue)) {
        size_t step_ran = 0U;

        status = sl_loop_run_once(loop, &step_ran);
        ran += step_ran;
        if (!sl_status_is_ok(status)) {
            break;
        }
    }

    loop->draining = false;
    if (out_ran != NULL) {
        *out_ran = ran;
    }

    return status;
}

void sl_loop_stop(SlLoop* loop)
{
    if (loop == NULL) {
        return;
    }

    loop->stopped = true;
}

size_t sl_loop_pending_count(const SlLoop* loop)
{
    return sl_ring_queue_count(loop == NULL ? NULL : &loop->queue);
}

size_t sl_loop_capacity(const SlLoop* loop)
{
    return sl_ring_queue_capacity(loop == NULL ? NULL : &loop->queue);
}

bool sl_loop_is_stopped(const SlLoop* loop)
{
    return loop != NULL && loop->stopped;
}
