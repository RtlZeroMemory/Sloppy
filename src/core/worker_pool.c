/*
 * src/core/worker_pool.c
 *
 * Implements Sloppy's inline worker-pool backend for deterministic tests.
 *
 * Safety invariants:
 * - no real threads, atomics, locks, platform APIs, libuv, V8, or heap allocation are used;
 * - work callbacks run immediately on the caller thread in this backend;
 * - completions are always posted to SlLoop instead of invoked directly;
 * - result ownership transfers to the completion callback only when the loop dispatches it;
 * - if posting fails or queued loop completions are deliberately discarded before dispatch,
 *   owned results are destroyed exactly once when a destroy callback is available.
 *
 * Tests: tests/unit/core/test_worker_pool.c.
 */
#include "sloppy/worker_pool.h"

static bool sl_work_kind_is_supported(SlWorkKind kind)
{
    return kind == SL_WORK_KIND_TEST || kind == SL_WORK_KIND_BLOCKING_IO ||
           kind == SL_WORK_KIND_CPU;
}

static void sl_worker_pool_clear_completion(SlWorkerPoolCompletionRecord* record)
{
    if (record == NULL) {
        return;
    }

    record->in_use = false;
    record->kind = SL_WORK_KIND_NONE;
    record->status = sl_status_ok();
    record->result = NULL;
    record->destroy_result = NULL;
    record->destroy_user = NULL;
    record->completion = NULL;
    record->completion_user = NULL;
}

static void sl_worker_pool_destroy_completion_result(SlWorkerPoolCompletionRecord* record)
{
    if (record != NULL && record->result != NULL && record->destroy_result != NULL) {
        record->destroy_result(record->result, record->destroy_user);
    }
}

static bool sl_worker_pool_has_pending_completion(const SlWorkerPool* pool)
{
    size_t index = 0U;

    if (pool == NULL) {
        return false;
    }

    for (index = 0U; index < SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY; index += 1U) {
        if (pool->completions[index].in_use) {
            return true;
        }
    }

    return false;
}

static SlWorkerPoolCompletionRecord* sl_worker_pool_reserve_completion(SlWorkerPool* pool)
{
    size_t index = 0U;

    if (pool == NULL) {
        return NULL;
    }

    for (index = 0U; index < SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY; index += 1U) {
        if (!pool->completions[index].in_use) {
            pool->completions[index].in_use = true;
            return &pool->completions[index];
        }
    }

    return NULL;
}

static SlStatus sl_worker_pool_dispatch(SlLoop* loop, SlCompletionKind kind, void* payload,
                                        void* user)
{
    SlWorkerPoolCompletionRecord* record = (SlWorkerPoolCompletionRecord*)payload;
    SlWorkCompletionFn completion = NULL;
    SlWorkKind work_kind = SL_WORK_KIND_NONE;
    SlStatus work_status;
    void* result = NULL;
    void* completion_user = NULL;

    (void)user;
    if (kind != SL_COMPLETION_KIND_USER || record == NULL || !record->in_use ||
        record->completion == NULL)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    completion = record->completion;
    work_kind = record->kind;
    work_status = record->status;
    result = record->result;
    completion_user = record->completion_user;

    sl_worker_pool_clear_completion(record);
    return completion(loop, work_kind, work_status, result, completion_user);
}

SlStatus sl_worker_pool_init_inline(SlWorkerPool* pool)
{
    size_t index = 0U;

    if (pool == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_worker_pool_has_pending_completion(pool)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    pool->mode = SL_WORKER_POOL_MODE_INLINE;
    for (index = 0U; index < SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY; index += 1U) {
        sl_worker_pool_clear_completion(&pool->completions[index]);
    }

    return sl_status_ok();
}

void sl_worker_pool_reset_inline(SlWorkerPool* pool)
{
    size_t index = 0U;

    if (pool == NULL) {
        return;
    }

    for (index = 0U; index < SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY; index += 1U) {
        sl_worker_pool_destroy_completion_result(&pool->completions[index]);
        sl_worker_pool_clear_completion(&pool->completions[index]);
    }
}

SlStatus sl_worker_pool_submit(SlWorkerPool* pool, SlLoop* completion_loop, const SlWorkItem* item,
                               SlWorkCompletionFn completion, void* completion_user)
{
    SlWorkerPoolCompletionRecord* record = NULL;
    SlStatus work_status;
    SlStatus post_status;
    void* result = NULL;

    if (pool == NULL || completion_loop == NULL || item == NULL || item->run == NULL ||
        completion == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (pool->mode != SL_WORKER_POOL_MODE_INLINE) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    if (!sl_work_kind_is_supported(item->kind)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    record = sl_worker_pool_reserve_completion(pool);
    if (record == NULL) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    work_status = item->run(item->payload, item->user, &result);
    record->kind = item->kind;
    record->status = work_status;
    record->result = result;
    record->destroy_result = item->destroy_result;
    record->destroy_user = item->user;
    record->completion = completion;
    record->completion_user = completion_user;

    post_status = sl_loop_post(completion_loop, SL_COMPLETION_KIND_USER, sl_worker_pool_dispatch,
                               record, NULL);
    if (!sl_status_is_ok(post_status)) {
        sl_worker_pool_destroy_completion_result(record);
        sl_worker_pool_clear_completion(record);
        return post_status;
    }

    return sl_status_ok();
}
