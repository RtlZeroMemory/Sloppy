/*
 * src/platform/libuv/thread.c
 *
 * Small Slop-owned thread primitive wrapper. Libuv types stay in platform code so core
 * provider executors can own worker lifecycle without exposing libuv or OS handles.
 */
#include "sloppy/platform_thread.h"

#include <stdbool.h>
#include <uv.h>

struct SlPlatformMutex
{
    uv_mutex_t mutex;
    bool initialized;
};

struct SlPlatformCond
{
    uv_cond_t cond;
    bool initialized;
};

struct SlPlatformThread
{
    uv_thread_t thread;
    bool started;
    bool joined;
};

typedef struct SlPlatformThreadStart
{
    SlPlatformThreadMainFn main_fn;
    void* user;
} SlPlatformThreadStart;

static SlStatus sl_platform_thread_status_from_uv(int status)
{
    if (status == 0) {
        return sl_status_ok();
    }
    if (status == UV_ENOMEM) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    if (status == UV_EINVAL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_status_from_code(SL_STATUS_INTERNAL);
}

SlStatus sl_platform_mutex_create(SlArena* arena, SlPlatformMutex** out_mutex)
{
    void* memory = NULL;
    SlPlatformMutex* mutex = NULL;
    SlStatus status;
    int uv_status = 0;

    if (out_mutex == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_mutex = NULL;
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_alloc(arena, sizeof(SlPlatformMutex), _Alignof(SlPlatformMutex), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    mutex = (SlPlatformMutex*)memory;
    *mutex = (SlPlatformMutex){0};
    uv_status = uv_mutex_init(&mutex->mutex);
    if (uv_status != 0) {
        return sl_platform_thread_status_from_uv(uv_status);
    }

    mutex->initialized = true;
    *out_mutex = mutex;
    return sl_status_ok();
}

void sl_platform_mutex_lock(SlPlatformMutex* mutex)
{
    if (mutex != NULL && mutex->initialized) {
        uv_mutex_lock(&mutex->mutex);
    }
}

void sl_platform_mutex_unlock(SlPlatformMutex* mutex)
{
    if (mutex != NULL && mutex->initialized) {
        uv_mutex_unlock(&mutex->mutex);
    }
}

void sl_platform_mutex_destroy(SlPlatformMutex* mutex)
{
    if (mutex != NULL && mutex->initialized) {
        uv_mutex_destroy(&mutex->mutex);
        mutex->initialized = false;
    }
}

SlStatus sl_platform_cond_create(SlArena* arena, SlPlatformCond** out_cond)
{
    void* memory = NULL;
    SlPlatformCond* cond = NULL;
    SlStatus status;
    int uv_status = 0;

    if (out_cond == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_cond = NULL;
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_alloc(arena, sizeof(SlPlatformCond), _Alignof(SlPlatformCond), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    cond = (SlPlatformCond*)memory;
    *cond = (SlPlatformCond){0};
    uv_status = uv_cond_init(&cond->cond);
    if (uv_status != 0) {
        return sl_platform_thread_status_from_uv(uv_status);
    }

    cond->initialized = true;
    *out_cond = cond;
    return sl_status_ok();
}

void sl_platform_cond_wait(SlPlatformCond* cond, SlPlatformMutex* mutex)
{
    if (cond != NULL && cond->initialized && mutex != NULL && mutex->initialized) {
        uv_cond_wait(&cond->cond, &mutex->mutex);
    }
}

void sl_platform_cond_signal(SlPlatformCond* cond)
{
    if (cond != NULL && cond->initialized) {
        uv_cond_signal(&cond->cond);
    }
}

void sl_platform_cond_broadcast(SlPlatformCond* cond)
{
    if (cond != NULL && cond->initialized) {
        uv_cond_broadcast(&cond->cond);
    }
}

void sl_platform_cond_destroy(SlPlatformCond* cond)
{
    if (cond != NULL && cond->initialized) {
        uv_cond_destroy(&cond->cond);
        cond->initialized = false;
    }
}

static void sl_platform_thread_trampoline(void* user)
{
    SlPlatformThreadStart* start = (SlPlatformThreadStart*)user;
    SlPlatformThreadMainFn main_fn = NULL;
    void* main_user = NULL;

    if (start == NULL || start->main_fn == NULL) {
        return;
    }

    main_fn = start->main_fn;
    main_user = start->user;
    main_fn(main_user);
}

SlStatus sl_platform_thread_start(SlArena* arena, SlPlatformThreadMainFn main_fn, void* user,
                                  SlPlatformThread** out_thread)
{
    void* thread_memory = NULL;
    void* start_memory = NULL;
    SlPlatformThread* thread = NULL;
    SlPlatformThreadStart* start = NULL;
    SlStatus status;
    int uv_status = 0;

    if (out_thread == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_thread = NULL;
    if (arena == NULL || main_fn == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status =
        sl_arena_alloc(arena, sizeof(SlPlatformThread), _Alignof(SlPlatformThread), &thread_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, sizeof(SlPlatformThreadStart), _Alignof(SlPlatformThreadStart),
                            &start_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    thread = (SlPlatformThread*)thread_memory;
    start = (SlPlatformThreadStart*)start_memory;
    *thread = (SlPlatformThread){0};
    *start = (SlPlatformThreadStart){main_fn, user};
    /*
     * SlPlatformThreadStart is arena-backed and read by sl_platform_thread_trampoline
     * on the spawned thread. The arena passed to sl_platform_thread_start must remain
     * live until the thread is joined.
     */
    uv_status = uv_thread_create(&thread->thread, sl_platform_thread_trampoline, start);
    if (uv_status != 0) {
        return sl_platform_thread_status_from_uv(uv_status);
    }

    thread->started = true;
    *out_thread = thread;
    return sl_status_ok();
}

void sl_platform_thread_join(SlPlatformThread* thread)
{
    if (thread != NULL && thread->started && !thread->joined) {
        uv_thread_join(&thread->thread);
        thread->joined = true;
    }
}
