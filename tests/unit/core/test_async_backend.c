#include "sloppy/async_backend.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct AsyncBackendRecord
{
    int values[8];
    SlStatusCode statuses[8];
    size_t count;
    size_t cleanup_count;
    size_t retain_count;
    size_t release_count;
    SlStatusCode dispatch_return;
    SlStatusCode retain_return;
    bool terminal;
    size_t late_count;
} AsyncBackendRecord;

typedef struct AsyncPayload
{
    AsyncBackendRecord* record;
    int value;
} AsyncPayload;

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
    AsyncBackendRecord* record = (AsyncBackendRecord*)scope;

    (void)user;
    if (record == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    record->retain_count += 1U;
    return sl_status_from_code(record->retain_return);
}

static void release_scope(void* scope, void* user)
{
    AsyncBackendRecord* record = (AsyncBackendRecord*)scope;

    (void)user;
    if (record != NULL) {
        record->release_count += 1U;
    }
}

static SlStatus record_completion(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                  void* user)
{
    AsyncBackendRecord* record = (AsyncBackendRecord*)user;
    AsyncPayload* payload = completion == NULL ? NULL : (AsyncPayload*)completion->payload;
    size_t index = 0U;

    (void)loop;
    if (record == NULL || payload == NULL || payload->record != record || record->count >= 8U) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->count;
    record->values[index] = payload->value;
    record->statuses[index] = sl_status_code(completion->status);
    record->count += 1U;
    return sl_status_from_code(record->dispatch_return);
}

static void cleanup_completion(const SlAsyncCompletion* completion, void* user)
{
    AsyncBackendRecord* record = (AsyncBackendRecord*)user;

    (void)completion;
    if (record != NULL) {
        record->cleanup_count += 1U;
    }
}

static bool completion_scope_is_terminal(const SlAsyncCompletion* completion, void* user)
{
    AsyncBackendRecord* record = (AsyncBackendRecord*)user;

    (void)completion;
    return record != NULL && record->terminal;
}

static void record_late_completion(const SlAsyncCompletion* completion, void* user)
{
    AsyncBackendRecord* record = (AsyncBackendRecord*)user;

    (void)completion;
    if (record != NULL) {
        record->late_count += 1U;
    }
}

static SlAsyncCompletion make_completion(AsyncBackendRecord* record, AsyncPayload* payload,
                                         int value)
{
    payload->record = record;
    payload->value = value;

    return (SlAsyncCompletion){
        .kind = SL_ASYNC_COMPLETION_TEST,
        .status = sl_status_ok(),
        .diag = NULL,
        .payload = payload,
        .operation = NULL,
        .scope = {.scope = record, .retain = retain_scope, .release = release_scope, .user = NULL},
        .dispatch = record_completion,
        .dispatch_user = record,
        .cleanup = cleanup_completion,
        .cleanup_user = record,
    };
}

static int test_create_post_drain_and_cleanup(void)
{
    unsigned char arena_storage[2048];
    SlArena arena;
    SlAsyncCompletion storage[3];
    SlAsyncLoop* loop = NULL;
    AsyncBackendRecord record = {.statuses = {SL_STATUS_OK}, .dispatch_return = SL_STATUS_OK};
    AsyncPayload first;
    AsyncPayload second;
    SlAsyncCompletion first_completion;
    SlAsyncCompletion second_completion;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 3U, &loop),
                      SL_STATUS_OK) != 0 ||
        loop == NULL)
    {
        return 1;
    }

    if (sl_async_loop_backend_kind(loop) != SL_ASYNC_BACKEND_TEST ||
        sl_async_loop_capacity(loop) != 3U || sl_async_loop_pending_count(loop) != 0U ||
        !sl_async_loop_is_owner_thread(loop))
    {
        return 2;
    }

    first_completion = make_completion(&record, &first, 11);
    second_completion = make_completion(&record, &second, 22);
    if (expect_status(sl_async_loop_post(loop, &first_completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_post(loop, &second_completion), SL_STATUS_OK) != 0 ||
        sl_async_loop_pending_count(loop) != 2U || record.retain_count != 2U)
    {
        return 3;
    }

    if (expect_status(sl_async_loop_run_once(loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 11 || record.cleanup_count != 1U ||
        record.release_count != 1U)
    {
        return 4;
    }

    if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 2U || record.values[1] != 22 || record.cleanup_count != 2U ||
        record.release_count != 2U || sl_async_loop_pending_count(loop) != 0U)
    {
        return 5;
    }

    sl_async_loop_dispose(loop);
    if (!sl_async_loop_is_disposed(loop) ||
        expect_status(sl_async_loop_post(loop, &first_completion), SL_STATUS_INVALID_STATE) != 0)
    {
        return 6;
    }

    return 0;
}

static int test_capacity_overflow_does_not_take_ownership(void)
{
    unsigned char arena_storage[2048];
    SlArena arena;
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = NULL;
    AsyncBackendRecord record = {.statuses = {SL_STATUS_OK}, .dispatch_return = SL_STATUS_OK};
    AsyncPayload first;
    AsyncPayload overflow;
    SlAsyncCompletion first_completion;
    SlAsyncCompletion overflow_completion;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 10;
    }

    first_completion = make_completion(&record, &first, 1);
    overflow_completion = make_completion(&record, &overflow, 2);
    if (expect_status(sl_async_loop_post(loop, &first_completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_post(loop, &overflow_completion),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        record.retain_count != 1U || record.cleanup_count != 0U || record.release_count != 0U)
    {
        return 11;
    }

    sl_async_loop_dispose(loop);
    if (record.cleanup_count != 1U || record.release_count != 1U) {
        return 12;
    }

    return 0;
}

static int test_retain_failure_does_not_enqueue_or_cleanup(void)
{
    unsigned char arena_storage[2048];
    SlArena arena;
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = NULL;
    AsyncBackendRecord record = {
        .statuses = {SL_STATUS_OK},
        .dispatch_return = SL_STATUS_OK,
        .retain_return = SL_STATUS_INVALID_STATE,
    };
    AsyncPayload payload;
    SlAsyncCompletion completion;
    size_t ran = 99U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 80;
    }

    completion = make_completion(&record, &payload, 7);
    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_INVALID_STATE) != 0 ||
        sl_async_loop_pending_count(loop) != 0U || record.retain_count != 1U ||
        record.release_count != 0U || record.cleanup_count != 0U)
    {
        sl_async_loop_dispose(loop);
        return 81;
    }

    if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 0U ||
        record.count != 0U)
    {
        sl_async_loop_dispose(loop);
        return 82;
    }

    sl_async_loop_dispose(loop);
    if (record.release_count != 0U || record.cleanup_count != 0U) {
        return 83;
    }

    return 0;
}

static int test_bounded_drain_preserves_pending_ownership_until_dispose(void)
{
    unsigned char arena_storage[2048];
    SlArena arena;
    SlAsyncCompletion storage[3];
    SlAsyncLoop* loop = NULL;
    AsyncBackendRecord record = {.statuses = {SL_STATUS_OK}, .dispatch_return = SL_STATUS_OK};
    AsyncPayload payloads[3];
    SlAsyncCompletion completions[3];
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 3U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 90;
    }

    for (size_t index = 0U; index < 3U; index += 1U) {
        completions[index] = make_completion(&record, &payloads[index], (int)(index + 1U));
        if (expect_status(sl_async_loop_post(loop, &completions[index]), SL_STATUS_OK) != 0) {
            sl_async_loop_dispose(loop);
            return 91;
        }
    }

    if (record.retain_count != 3U || sl_async_loop_pending_count(loop) != 3U ||
        expect_status(sl_async_loop_drain(loop, 2U, &ran), SL_STATUS_OK) != 0 || ran != 2U ||
        record.count != 2U || record.cleanup_count != 2U || record.release_count != 2U ||
        sl_async_loop_pending_count(loop) != 1U)
    {
        sl_async_loop_dispose(loop);
        return 92;
    }

    sl_async_loop_dispose(loop);
    sl_async_loop_dispose(loop);
    if (!sl_async_loop_is_disposed(loop) || record.count != 2U || record.cleanup_count != 3U ||
        record.release_count != 3U || sl_async_loop_pending_count(loop) != 0U)
    {
        return 93;
    }

    return 0;
}

static int test_dispatch_failure_still_cleans_once(void)
{
    unsigned char arena_storage[2048];
    SlArena arena;
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = NULL;
    AsyncBackendRecord record = {.statuses = {SL_STATUS_OK}, .dispatch_return = SL_STATUS_INTERNAL};
    AsyncPayload payload;
    SlAsyncCompletion completion;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 20;
    }

    completion = make_completion(&record, &payload, 5);
    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_INTERNAL) != 0 || ran != 1U ||
        record.count != 1U || record.cleanup_count != 1U || record.release_count != 1U ||
        sl_async_loop_pending_count(loop) != 0U)
    {
        return 21;
    }

    sl_async_loop_dispose(loop);
    if (record.cleanup_count != 1U || record.release_count != 1U) {
        return 22;
    }

    return 0;
}

static int test_terminal_completion_is_cleanup_only(void)
{
    unsigned char arena_storage[2048];
    SlArena arena;
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = NULL;
    AsyncBackendRecord record = {
        .statuses = {SL_STATUS_OK}, .dispatch_return = SL_STATUS_OK, .terminal = true};
    AsyncPayload payload;
    SlAsyncCompletion completion;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 50;
    }

    completion = make_completion(&record, &payload, 99);
    completion.terminal_check = completion_scope_is_terminal;
    completion.terminal_check_user = &record;
    completion.late = record_late_completion;
    completion.late_user = &record;

    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 0U || record.late_count != 1U || record.cleanup_count != 1U ||
        record.release_count != 1U || sl_async_loop_pending_count(loop) != 0U)
    {
        return 51;
    }

    sl_async_loop_dispose(loop);
    if (record.cleanup_count != 1U || record.release_count != 1U || record.late_count != 1U) {
        return 52;
    }

    return 0;
}

static int test_completion_terminal_after_enqueue_is_cleanup_only(void)
{
    unsigned char arena_storage[2048];
    SlArena arena;
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = NULL;
    AsyncBackendRecord record = {.statuses = {SL_STATUS_OK}, .dispatch_return = SL_STATUS_OK};
    AsyncPayload payload;
    SlAsyncCompletion completion;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 60;
    }

    completion = make_completion(&record, &payload, 123);
    completion.terminal_check = completion_scope_is_terminal;
    completion.terminal_check_user = &record;
    completion.late = record_late_completion;
    completion.late_user = &record;

    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_OK) != 0 ||
        record.retain_count != 1U)
    {
        return 61;
    }

    record.terminal = true;
    if (expect_status(sl_async_loop_run_once(loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 0U || record.late_count != 1U || record.cleanup_count != 1U ||
        record.release_count != 1U || sl_async_loop_pending_count(loop) != 0U)
    {
        return 62;
    }

    sl_async_loop_dispose(loop);
    if (record.cleanup_count != 1U || record.release_count != 1U || record.late_count != 1U) {
        return 63;
    }

    return 0;
}

static int test_invalid_arguments(void)
{
    unsigned char arena_storage[512];
    SlArena arena;
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = NULL;
    size_t used_before = 0U;
    AsyncBackendRecord record = {.statuses = {SL_STATUS_OK}, .dispatch_return = SL_STATUS_OK};
    AsyncPayload payload;
    SlAsyncCompletion completion;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 30;
    }

    used_before = sl_arena_used(&arena);
    completion = make_completion(&record, &payload, 1);
    if (expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, NULL, storage, 1U, &loop),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, storage, 1U, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_NONE, &arena, storage, 1U, &loop),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        sl_arena_used(&arena) != used_before ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, NULL, 1U, &loop),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, NULL, 0U, &loop),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_post(NULL, &completion), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_async_loop_post(loop, NULL), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 31;
    }

    sl_async_loop_dispose(loop);
    return 0;
}

static int test_libuv_init_failure_rolls_back_arena(void)
{
    unsigned char arena_storage[512];
    SlArena arena;
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = NULL;
    size_t used_before = 0U;
    SlStatus status;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 70;
    }

    used_before = sl_arena_used(&arena);
    status = sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 1U, &loop);
    if (sl_status_is_ok(status) || sl_status_code(status) != SL_STATUS_OUT_OF_MEMORY ||
        loop != NULL || sl_arena_used(&arena) != used_before)
    {
        if (loop != NULL) {
            sl_async_loop_dispose(loop);
        }
        return 71;
    }

    return 0;
}

int main(void)
{
    int result = test_create_post_drain_and_cleanup();

    if (result != 0) {
        return result;
    }

    result = test_capacity_overflow_does_not_take_ownership();
    if (result != 0) {
        return result;
    }

    result = test_retain_failure_does_not_enqueue_or_cleanup();
    if (result != 0) {
        return result;
    }

    result = test_bounded_drain_preserves_pending_ownership_until_dispose();
    if (result != 0) {
        return result;
    }

    result = test_dispatch_failure_still_cleans_once();
    if (result != 0) {
        return result;
    }

    result = test_terminal_completion_is_cleanup_only();
    if (result != 0) {
        return result;
    }

    result = test_completion_terminal_after_enqueue_is_cleanup_only();
    if (result != 0) {
        return result;
    }

    result = test_invalid_arguments();
    if (result != 0) {
        return result;
    }

    return test_libuv_init_failure_rolls_back_arena();
}
