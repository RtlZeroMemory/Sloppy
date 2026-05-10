#include "sloppy/platform_thread.h"

#include <stdbool.h>

typedef struct PlatformThreadState
{
    SlPlatformMutex* mutex;
    SlPlatformCond* cond;
    bool ready;
    bool done;
    size_t handoff_count;
} PlatformThreadState;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode expected)
{
    return expect_true(sl_status_code(status) == expected);
}

static void platform_thread_handoff_main(void* user)
{
    PlatformThreadState* state = (PlatformThreadState*)user;

    if (state == NULL || state->mutex == NULL || state->cond == NULL) {
        return;
    }

    sl_platform_mutex_lock(state->mutex);
    while (!state->ready) {
        sl_platform_cond_wait(state->cond, state->mutex);
    }
    state->handoff_count += 1U;
    state->done = true;
    sl_platform_cond_broadcast(state->cond);
    sl_platform_mutex_unlock(state->mutex);
}

static int test_platform_thread_handoff_and_idempotent_cleanup(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlatformMutex* mutex = NULL;
    SlPlatformCond* cond = NULL;
    SlPlatformThread* thread = NULL;
    PlatformThreadState state = {0};

    sl_platform_mutex_lock(NULL);
    sl_platform_mutex_unlock(NULL);
    sl_platform_mutex_destroy(NULL);
    sl_platform_cond_signal(NULL);
    sl_platform_cond_broadcast(NULL);
    sl_platform_cond_destroy(NULL);
    sl_platform_thread_join(NULL);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_platform_mutex_create(NULL, &mutex), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_platform_mutex_create(&arena, NULL), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_platform_cond_create(NULL, &cond), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_platform_cond_create(&arena, NULL), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_platform_thread_start(&arena, NULL, NULL, &thread),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_platform_thread_start(&arena, platform_thread_handoff_main, NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_platform_mutex_create(&arena, &mutex), SL_STATUS_OK) != 0 ||
        expect_status(sl_platform_cond_create(&arena, &cond), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    state.mutex = mutex;
    state.cond = cond;
    if (expect_status(
            sl_platform_thread_start(&arena, platform_thread_handoff_main, &state, &thread),
            SL_STATUS_OK) != 0 ||
        thread == NULL)
    {
        sl_platform_cond_destroy(cond);
        sl_platform_mutex_destroy(mutex);
        return 2;
    }

    sl_platform_mutex_lock(mutex);
    state.ready = true;
    sl_platform_cond_signal(cond);
    while (!state.done) {
        sl_platform_cond_wait(cond, mutex);
    }
    sl_platform_mutex_unlock(mutex);

    sl_platform_thread_join(thread);
    thread = NULL;
    sl_platform_thread_join(thread);
    if (state.handoff_count != 1U || !state.done) {
        sl_platform_cond_destroy(cond);
        sl_platform_mutex_destroy(mutex);
        return 3;
    }

    sl_platform_cond_destroy(cond);
    cond = NULL;
    sl_platform_cond_destroy(cond);
    sl_platform_mutex_destroy(mutex);
    mutex = NULL;
    sl_platform_mutex_destroy(mutex);
    return 0;
}

int main(void)
{
    return test_platform_thread_handoff_and_idempotent_cleanup();
}
