#include "sloppy/async_backend.h"

#include <cstddef>
#include <thread>

typedef struct LibuvRecord
{
    int values[8];
    size_t count;
    size_t cleanup_count;
    size_t release_count;
} LibuvRecord;

typedef struct LibuvPayload
{
    LibuvRecord* record;
    int value;
} LibuvPayload;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlStatus retain_scope(void* scope, void* user)
{
    (void)scope;
    (void)user;
    return sl_status_ok();
}

static void release_scope(void* scope, void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(scope);

    (void)user;
    if (record != nullptr) {
        record->release_count += 1U;
    }
}

static SlStatus record_completion(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                  void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(user);
    LibuvPayload* payload =
        completion == nullptr ? nullptr : static_cast<LibuvPayload*>(completion->payload);
    size_t index = 0U;

    if (loop == nullptr || !sl_async_loop_is_owner_thread(loop) || record == nullptr ||
        payload == nullptr || payload->record != record || record->count >= 8U)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->count;
    record->values[index] = payload->value;
    record->count += 1U;
    return sl_status_ok();
}

static void cleanup_completion(const SlAsyncCompletion* completion, void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(user);

    (void)completion;
    if (record != nullptr) {
        record->cleanup_count += 1U;
    }
}

static SlAsyncCompletion make_completion(LibuvRecord* record, LibuvPayload* payload, int value)
{
    payload->record = record;
    payload->value = value;

    SlAsyncCompletion completion = {};
    completion.kind = SL_ASYNC_COMPLETION_TEST;
    completion.status = sl_status_ok();
    completion.payload = payload;
    completion.scope.scope = record;
    completion.scope.retain = retain_scope;
    completion.scope.release = release_scope;
    completion.dispatch = record_completion;
    completion.dispatch_user = record;
    completion.cleanup = cleanup_completion;
    completion.cleanup_user = record;
    return completion;
}

static int test_libuv_create_post_drain_dispose(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[2];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U};
    LibuvPayload payload;
    SlAsyncCompletion completion;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 2U, &loop),
                      SL_STATUS_OK) != 0 ||
        loop == nullptr)
    {
        return 1;
    }

    if (sl_async_loop_backend_kind(loop) != SL_ASYNC_BACKEND_LIBUV ||
        sl_async_loop_capacity(loop) != 2U || !sl_async_loop_is_owner_thread(loop))
    {
        sl_async_loop_dispose(loop);
        return 2;
    }

    completion = make_completion(&record, &payload, 31);
    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 31 || record.cleanup_count != 1U ||
        record.release_count != 1U)
    {
        sl_async_loop_dispose(loop);
        return 3;
    }

    sl_async_loop_dispose(loop);
    if (!sl_async_loop_is_disposed(loop) ||
        expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_INVALID_STATE) != 0)
    {
        return 4;
    }

    return 0;
}

static int test_libuv_cross_thread_post_owner_thread_dispatch(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[2];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U};
    LibuvPayload payload;
    SlAsyncCompletion completion;
    SlStatus worker_status = sl_status_ok();
    bool worker_saw_owner = true;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 2U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 10;
    }

    completion = make_completion(&record, &payload, 44);
    std::thread worker([&]() {
        worker_saw_owner = sl_async_loop_is_owner_thread(loop);
        worker_status = sl_async_loop_post(loop, &completion);
    });
    worker.join();

    if (worker_saw_owner || expect_status(worker_status, SL_STATUS_OK) != 0 || record.count != 0U ||
        sl_async_loop_pending_count(loop) != 1U)
    {
        sl_async_loop_dispose(loop);
        return 11;
    }

    if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 44 || record.cleanup_count != 1U ||
        record.release_count != 1U)
    {
        sl_async_loop_dispose(loop);
        return 12;
    }

    sl_async_loop_dispose(loop);
    return 0;
}

static int test_libuv_capacity_overflow_is_deterministic(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U};
    LibuvPayload first;
    LibuvPayload second;
    SlAsyncCompletion first_completion;
    SlAsyncCompletion second_completion;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 20;
    }

    first_completion = make_completion(&record, &first, 1);
    second_completion = make_completion(&record, &second, 2);
    if (expect_status(sl_async_loop_post(loop, &first_completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_post(loop, &second_completion), SL_STATUS_CAPACITY_EXCEEDED) !=
            0 ||
        sl_async_loop_pending_count(loop) != 1U || record.cleanup_count != 0U ||
        record.release_count != 0U)
    {
        sl_async_loop_dispose(loop);
        return 21;
    }

    sl_async_loop_dispose(loop);
    if (record.cleanup_count != 1U || record.release_count != 1U) {
        return 22;
    }

    return 0;
}

int main(void)
{
    int result = test_libuv_create_post_drain_dispose();

    if (result != 0) {
        return result;
    }

    result = test_libuv_cross_thread_post_owner_thread_dispatch();
    if (result != 0) {
        return result;
    }

    return test_libuv_capacity_overflow_is_deterministic();
}
