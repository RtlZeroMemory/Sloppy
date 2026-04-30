/*
 * src/platform/libuv/async_backend_libuv.c
 *
 * Libuv-backed implementation of Sloppy's async backend. Libuv handles and thread IDs stay
 * inside this file; public Slop headers expose only Slop-owned async types.
 */
#include "../../core/async_backend_internal.h"

#include <uv.h>

typedef struct SlLibuvAsyncBackend
{
    uv_loop_t loop;
    uv_async_t async;
    uv_mutex_t mutex;
    uv_thread_t owner;
    bool mutex_initialized;
    bool loop_initialized;
    bool async_initialized;
    bool closing;
} SlLibuvAsyncBackend;

static SlStatus sl_libuv_status_from_error(int error)
{
    if (error == 0) {
        return sl_status_ok();
    }

    if (error == UV_ENOMEM) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    if (error == UV_EINVAL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (error == UV_EBUSY) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    return sl_status_from_code(SL_STATUS_INTERNAL);
}

static SlLibuvAsyncBackend* sl_libuv_backend(SlAsyncLoop* loop)
{
    return loop == NULL ? NULL : (SlLibuvAsyncBackend*)loop->backend;
}

static const SlLibuvAsyncBackend* sl_libuv_backend_const(const SlAsyncLoop* loop)
{
    return loop == NULL ? NULL : (const SlLibuvAsyncBackend*)loop->backend;
}

static void sl_libuv_async_wakeup(uv_async_t* handle)
{
    (void)handle;
}

SlStatus sl_async_loop_libuv_init(SlAsyncLoop* loop, SlArena* arena)
{
    void* memory = NULL;
    SlLibuvAsyncBackend* backend = NULL;
    SlStatus alloc_status;
    int uv_status = 0;

    if (loop == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    alloc_status =
        sl_arena_alloc(arena, sizeof(SlLibuvAsyncBackend), _Alignof(SlLibuvAsyncBackend), &memory);
    if (!sl_status_is_ok(alloc_status)) {
        return alloc_status;
    }

    backend = (SlLibuvAsyncBackend*)memory;
    *backend = (SlLibuvAsyncBackend){0};
    backend->owner = uv_thread_self();

    uv_status = uv_loop_init(&backend->loop);
    if (uv_status != 0) {
        return sl_libuv_status_from_error(uv_status);
    }
    backend->loop_initialized = true;

    uv_status = uv_mutex_init(&backend->mutex);
    if (uv_status != 0) {
        (void)uv_loop_close(&backend->loop);
        return sl_libuv_status_from_error(uv_status);
    }
    backend->mutex_initialized = true;

    uv_status = uv_async_init(&backend->loop, &backend->async, sl_libuv_async_wakeup);
    if (uv_status != 0) {
        uv_mutex_destroy(&backend->mutex);
        (void)uv_loop_close(&backend->loop);
        return sl_libuv_status_from_error(uv_status);
    }
    backend->async_initialized = true;
    backend->async.data = loop;
    loop->backend = backend;
    return sl_status_ok();
}

bool sl_async_loop_libuv_is_owner_thread(const SlAsyncLoop* loop)
{
    const SlLibuvAsyncBackend* backend = sl_libuv_backend_const(loop);
    uv_thread_t current;

    if (backend == NULL) {
        return false;
    }

    current = uv_thread_self();
    return uv_thread_equal(&backend->owner, &current) != 0;
}

SlStatus sl_async_loop_libuv_post(SlAsyncLoop* loop, const SlAsyncCompletion* completion)
{
    SlLibuvAsyncBackend* backend = sl_libuv_backend(loop);
    SlAsyncCompletion rollback_completion;
    SlStatus status;
    int uv_status = 0;
    bool rolled_back = false;

    if (loop == NULL || backend == NULL || completion == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->disposed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    uv_mutex_lock(&backend->mutex);
    if (backend->closing) {
        uv_mutex_unlock(&backend->mutex);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    status = sl_async_loop_enqueue_owned(loop, completion);
    if (!sl_status_is_ok(status)) {
        uv_mutex_unlock(&backend->mutex);
        return status;
    }

    uv_status = uv_async_send(&backend->async);
    if (uv_status != 0) {
        rolled_back = sl_async_loop_unenqueue_last_owned(loop, &rollback_completion);
    }
    uv_mutex_unlock(&backend->mutex);

    if (rolled_back) {
        sl_async_loop_release_completion_scope(&rollback_completion);
    }
    return sl_libuv_status_from_error(uv_status);
}

static SlStatus sl_async_loop_libuv_pop_one(SlAsyncLoop* loop, SlAsyncCompletion* out_completion)
{
    SlLibuvAsyncBackend* backend = sl_libuv_backend(loop);
    bool popped = false;

    if (backend == NULL || out_completion == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    uv_mutex_lock(&backend->mutex);
    popped = sl_async_loop_pop(loop, out_completion);
    uv_mutex_unlock(&backend->mutex);

    return popped ? sl_status_ok() : sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

SlStatus sl_async_loop_libuv_run_once(SlAsyncLoop* loop, size_t* out_ran)
{
    SlLibuvAsyncBackend* backend = sl_libuv_backend(loop);
    SlAsyncCompletion completion;
    SlStatus status;

    if (out_ran != NULL) {
        *out_ran = 0U;
    }

    if (loop == NULL || backend == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (loop->disposed || backend->closing) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (!sl_async_loop_libuv_is_owner_thread(loop)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    (void)uv_run(&backend->loop, UV_RUN_NOWAIT);

    status = sl_async_loop_libuv_pop_one(loop, &completion);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        return sl_status_ok();
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (out_ran != NULL) {
        *out_ran = 1U;
    }

    return sl_async_loop_dispatch_completion(loop, &completion);
}

SlStatus sl_async_loop_libuv_drain(SlAsyncLoop* loop, size_t max_count, size_t* out_ran)
{
    SlStatus status = sl_status_ok();
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
    while (max_count == 0U || ran < max_count) {
        size_t step_ran = 0U;

        status = sl_async_loop_libuv_run_once(loop, &step_ran);
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

void sl_async_loop_libuv_dispose(SlAsyncLoop* loop)
{
    SlLibuvAsyncBackend* backend = sl_libuv_backend(loop);

    if (loop == NULL || backend == NULL || loop->disposed) {
        return;
    }

    if (backend->mutex_initialized) {
        SlAsyncCompletion completion;
        bool popped = false;

        uv_mutex_lock(&backend->mutex);
        backend->closing = true;
        loop->disposed = true;
        uv_mutex_unlock(&backend->mutex);

        do {
            uv_mutex_lock(&backend->mutex);
            popped = sl_async_loop_pop(loop, &completion);
            uv_mutex_unlock(&backend->mutex);

            if (popped) {
                sl_async_loop_finish_completion(&completion);
            }
        } while (popped);
    }
    else {
        backend->closing = true;
        loop->disposed = true;
    }

    if (backend->async_initialized && !uv_is_closing((uv_handle_t*)&backend->async)) {
        uv_close((uv_handle_t*)&backend->async, NULL);
    }

    if (backend->loop_initialized) {
        while (uv_run(&backend->loop, UV_RUN_DEFAULT) != 0) {
        }
        (void)uv_loop_close(&backend->loop);
    }

    if (backend->mutex_initialized) {
        uv_mutex_destroy(&backend->mutex);
        backend->mutex_initialized = false;
    }
}
