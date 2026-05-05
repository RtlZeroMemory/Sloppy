/*
 * src/core/async_backend.c
 *
 * Implements Sloppy's async backend abstraction and deterministic test backend. The real
 * libuv backend lives under src/platform/libuv/ so libuv types never enter public headers
 * or core module state.
 */
#include "async_backend_internal.h"

static void sl_async_completion_clear(SlAsyncCompletion* completion)
{
    if (completion == NULL) {
        return;
    }

    *completion = (SlAsyncCompletion){0};
}

static bool sl_async_completion_valid(const SlAsyncCompletion* completion)
{
    return completion != NULL && completion->kind != SL_ASYNC_COMPLETION_NONE &&
           completion->dispatch != NULL;
}

static SlStatus sl_async_completion_retain_scope(const SlAsyncCompletion* completion)
{
    if (completion == NULL || completion->scope.scope == NULL || completion->scope.retain == NULL) {
        return sl_status_ok();
    }

    return completion->scope.retain(completion->scope.scope, completion->scope.user);
}

SlStatus sl_async_loop_common_init(SlAsyncLoop* loop, SlAsyncBackendKind kind, SlArena* arena,
                                   SlAsyncCompletion* storage, size_t capacity)
{
    size_t index = 0U;

    if (loop == NULL || arena == NULL || (storage == NULL && capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    loop->kind = kind;
    loop->arena = arena;
    loop->queue = storage;
    loop->capacity = capacity;
    loop->head = 0U;
    loop->tail = 0U;
    loop->count = 0U;
    loop->disposed = false;
    loop->draining = false;
    loop->backend = NULL;

    for (index = 0U; index < capacity; index += 1U) {
        sl_async_completion_clear(&storage[index]);
    }

    return sl_status_ok();
}

SlStatus sl_async_loop_enqueue_owned(SlAsyncLoop* loop, const SlAsyncCompletion* completion)
{
    SlStatus retain_status;

    if (loop == NULL || !sl_async_completion_valid(completion)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->disposed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (loop->count >= loop->capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    if (loop->queue == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    retain_status = sl_async_completion_retain_scope(completion);
    if (!sl_status_is_ok(retain_status)) {
        return retain_status;
    }

    loop->queue[loop->tail] = *completion;
    loop->tail = (loop->tail + 1U) % loop->capacity;
    loop->count += 1U;
    return sl_status_ok();
}

bool sl_async_loop_unenqueue_last_owned(SlAsyncLoop* loop, SlAsyncCompletion* out_completion)
{
    size_t tail = 0U;

    if (out_completion != NULL) {
        sl_async_completion_clear(out_completion);
    }

    if (loop == NULL || out_completion == NULL || loop->count == 0U || loop->queue == NULL ||
        loop->capacity == 0U)
    {
        return false;
    }

    tail = (loop->tail + loop->capacity - 1U) % loop->capacity;
    *out_completion = loop->queue[tail];
    sl_async_completion_clear(&loop->queue[tail]);
    loop->tail = tail;
    loop->count -= 1U;
    return true;
}

bool sl_async_loop_pop(SlAsyncLoop* loop, SlAsyncCompletion* out_completion)
{
    if (out_completion != NULL) {
        sl_async_completion_clear(out_completion);
    }

    if (loop == NULL || out_completion == NULL || loop->count == 0U || loop->queue == NULL ||
        loop->capacity == 0U)
    {
        return false;
    }

    *out_completion = loop->queue[loop->head];
    sl_async_completion_clear(&loop->queue[loop->head]);
    loop->head = (loop->head + 1U) % loop->capacity;
    loop->count -= 1U;
    return true;
}

void sl_async_loop_release_completion_scope(const SlAsyncCompletion* completion)
{
    if (completion == NULL) {
        return;
    }

    if (completion->scope.scope != NULL && completion->scope.release != NULL) {
        completion->scope.release(completion->scope.scope, completion->scope.user);
    }
}

void sl_async_loop_finish_completion(const SlAsyncCompletion* completion)
{
    if (completion == NULL) {
        return;
    }

    if (completion->cleanup != NULL) {
        completion->cleanup(completion, completion->cleanup_user);
    }

    sl_async_loop_release_completion_scope(completion);
}

static bool sl_async_completion_is_terminal(const SlAsyncCompletion* completion)
{
    return completion != NULL && completion->terminal_check != NULL &&
           completion->terminal_check(completion, completion->terminal_check_user);
}

SlStatus sl_async_loop_dispatch_completion(SlAsyncLoop* loop, const SlAsyncCompletion* completion)
{
    SlStatus status;

    if (loop == NULL || completion == NULL || completion->dispatch == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_async_completion_is_terminal(completion)) {
        if (completion->late != NULL) {
            completion->late(completion, completion->late_user);
        }
        sl_async_loop_finish_completion(completion);
        return sl_status_ok();
    }

    status = completion->dispatch(loop, completion, completion->dispatch_user);
    sl_async_loop_finish_completion(completion);
    return status;
}

void sl_async_loop_discard_pending_unlocked(SlAsyncLoop* loop)
{
    SlAsyncCompletion completion;

    if (loop == NULL) {
        return;
    }

    while (sl_async_loop_pop(loop, &completion)) {
        sl_async_loop_finish_completion(&completion);
    }
}

static SlStatus sl_async_loop_test_run_once(SlAsyncLoop* loop, size_t* out_ran)
{
    SlAsyncCompletion completion;

    if (out_ran != NULL) {
        *out_ran = 0U;
    }

    if (loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->disposed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (!sl_async_loop_pop(loop, &completion)) {
        return sl_status_ok();
    }

    if (out_ran != NULL) {
        *out_ran = 1U;
    }

    return sl_async_loop_dispatch_completion(loop, &completion);
}

static SlStatus sl_async_loop_test_drain(SlAsyncLoop* loop, size_t max_count, size_t* out_ran)
{
    SlStatus status = sl_status_ok();
    size_t ran = 0U;

    if (out_ran != NULL) {
        *out_ran = 0U;
    }

    if (loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->disposed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (loop->draining) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    loop->draining = true;
    while (loop->count != 0U && (max_count == 0U || ran < max_count)) {
        size_t step_ran = 0U;

        status = sl_async_loop_test_run_once(loop, &step_ran);
        ran += step_ran;
        if (!sl_status_is_ok(status) || step_ran == 0U) {
            break;
        }
    }
    loop->draining = false;

    if (out_ran != NULL) {
        *out_ran = ran;
    }

    return status;
}

SlStatus sl_async_loop_create(SlAsyncBackendKind kind, SlArena* arena, SlAsyncCompletion* storage,
                              size_t capacity, SlAsyncLoop** out_loop)
{
    void* memory = NULL;
    SlAsyncLoop* loop = NULL;
    SlStatus status;

    if (out_loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_loop = NULL;
    if (arena == NULL || (storage == NULL && capacity != 0U) || kind == SL_ASYNC_BACKEND_NONE) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_alloc(arena, sizeof(SlAsyncLoop), _Alignof(SlAsyncLoop), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    loop = (SlAsyncLoop*)memory;
    status = sl_async_loop_common_init(loop, kind, arena, storage, capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (kind == SL_ASYNC_BACKEND_LIBUV) {
        status = sl_async_loop_libuv_init(loop, arena);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    else if (kind != SL_ASYNC_BACKEND_TEST) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_loop = loop;
    return sl_status_ok();
}

void sl_async_loop_dispose(SlAsyncLoop* loop)
{
    if (loop == NULL || loop->disposed) {
        return;
    }

    if (loop->kind == SL_ASYNC_BACKEND_LIBUV) {
        sl_async_loop_libuv_dispose(loop);
        return;
    }

    loop->disposed = true;
    sl_async_loop_discard_pending_unlocked(loop);
}

SlStatus sl_async_loop_post(SlAsyncLoop* loop, const SlAsyncCompletion* completion)
{
    if (loop == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->kind == SL_ASYNC_BACKEND_LIBUV) {
        return sl_async_loop_libuv_post(loop, completion);
    }

    return sl_async_loop_enqueue_owned(loop, completion);
}

SlStatus sl_async_loop_run_once(SlAsyncLoop* loop, size_t* out_ran)
{
    if (loop == NULL) {
        if (out_ran != NULL) {
            *out_ran = 0U;
        }
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->kind == SL_ASYNC_BACKEND_LIBUV) {
        return sl_async_loop_libuv_run_once(loop, out_ran);
    }

    return sl_async_loop_test_run_once(loop, out_ran);
}

SlStatus sl_async_loop_drain(SlAsyncLoop* loop, size_t max_count, size_t* out_ran)
{
    if (loop == NULL) {
        if (out_ran != NULL) {
            *out_ran = 0U;
        }
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->kind == SL_ASYNC_BACKEND_LIBUV) {
        return sl_async_loop_libuv_drain(loop, max_count, out_ran);
    }

    return sl_async_loop_test_drain(loop, max_count, out_ran);
}

SlAsyncBackendKind sl_async_loop_backend_kind(const SlAsyncLoop* loop)
{
    return loop == NULL ? SL_ASYNC_BACKEND_NONE : loop->kind;
}

size_t sl_async_loop_pending_count(const SlAsyncLoop* loop)
{
    return loop == NULL ? 0U : loop->count;
}

size_t sl_async_loop_capacity(const SlAsyncLoop* loop)
{
    return loop == NULL ? 0U : loop->capacity;
}

bool sl_async_loop_is_owner_thread(const SlAsyncLoop* loop)
{
    if (loop == NULL) {
        return false;
    }

    if (loop->kind == SL_ASYNC_BACKEND_LIBUV) {
        return sl_async_loop_libuv_is_owner_thread(loop);
    }

    return !loop->disposed;
}

bool sl_async_loop_is_disposed(const SlAsyncLoop* loop)
{
    return loop != NULL && loop->disposed;
}
