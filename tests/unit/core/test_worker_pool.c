#include "sloppy/worker_pool.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct WorkerRecord
{
    SlWorkKind kinds[12];
    SlStatusCode statuses[12];
    void* results[12];
    void* users[12];
    int values[12];
    size_t count;
    size_t run_count;
    size_t destroy_count;
    SlStatusCode completion_return_code;
} WorkerRecord;

typedef struct WorkerPayload
{
    WorkerRecord* record;
    int value;
    void* result;
    SlStatusCode return_code;
} WorkerPayload;

typedef struct DestroyUser
{
    WorkerRecord* record;
    void* destroyed_result;
} DestroyUser;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlStatus run_recording_work(void* payload, void* user, void** out_result)
{
    WorkerPayload* worker_payload = (WorkerPayload*)payload;
    WorkerRecord* record = worker_payload == NULL ? NULL : worker_payload->record;

    (void)user;
    if (out_result == NULL || worker_payload == NULL || record == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    record->run_count += 1U;
    *out_result = worker_payload->result;
    return sl_status_from_code(worker_payload->return_code);
}

static SlStatus run_null_payload_work(void* payload, void* user, void** out_result)
{
    WorkerRecord* record = (WorkerRecord*)user;

    if (payload != NULL || out_result == NULL || record == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    record->run_count += 1U;
    *out_result = NULL;
    return sl_status_ok();
}

static SlStatus record_completion(SlLoop* loop, SlWorkKind kind, SlStatus status, void* result,
                                  void* user)
{
    WorkerRecord* record = (WorkerRecord*)user;
    WorkerPayload* worker_payload = (WorkerPayload*)result;
    size_t index = 0U;

    (void)loop;
    if (record == NULL || record->count >= 12U) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->count;
    record->kinds[index] = kind;
    record->statuses[index] = sl_status_code(status);
    record->results[index] = result;
    record->users[index] = user;
    record->values[index] = worker_payload == NULL ? -1 : worker_payload->value;
    record->count += 1U;

    return sl_status_from_code(record->completion_return_code);
}

static SlStatus noop_loop_completion(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    (void)loop;
    (void)kind;
    (void)payload;
    (void)user;
    return sl_status_ok();
}

static SlStatus expect_null_completion_user(SlLoop* loop, SlWorkKind kind, SlStatus status,
                                            void* result, void* user)
{
    (void)loop;
    if (kind != SL_WORK_KIND_TEST || sl_status_code(status) != SL_STATUS_OK || result != NULL ||
        user != NULL)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    return sl_status_ok();
}

static void destroy_result(void* result, void* user)
{
    DestroyUser* destroy_user = (DestroyUser*)user;

    if (destroy_user != NULL && destroy_user->record != NULL) {
        destroy_user->record->destroy_count += 1U;
        destroy_user->destroyed_result = result;
    }
}

static int test_initialization(void)
{
    SlWorkerPool pool = {0};

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        pool.mode != SL_WORKER_POOL_MODE_INLINE)
    {
        return 1;
    }

    if (expect_status(sl_worker_pool_init_inline(NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 2;
    }

    return 0;
}

static int test_submit_validation(void)
{
    SlWorkerPool pool = {0};
    SlWorkerPool uninitialized = {0};
    SlCompletion storage[1];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    WorkerPayload payload = {&record, 7, NULL, SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &payload, &record, NULL};
    SlWorkItem invalid_kind = {SL_WORK_KIND_NONE, run_recording_work, &payload, &record, NULL};
    SlWorkItem no_run = {SL_WORK_KIND_TEST, NULL, &payload, &record, NULL};

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0)
    {
        return 10;
    }

    if (expect_status(sl_worker_pool_submit(NULL, &loop, &item, record_completion, &record),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, NULL, &item, record_completion, &record),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, &loop, NULL, record_completion, &record),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, &loop, &no_run, record_completion, &record),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, &loop, &item, NULL, &record),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(
            sl_worker_pool_submit(&pool, &loop, &invalid_kind, record_completion, &record),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 11;
    }

    if (expect_status(
            sl_worker_pool_submit(&uninitialized, &loop, &item, record_completion, &record),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 12;
    }

    if (record.run_count != 0U || sl_loop_pending_count(&loop) != 0U) {
        return 13;
    }

    return 0;
}

static int test_inline_execution_posts_completion(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[2];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    WorkerPayload result_payload = {&record, 42, NULL, SL_STATUS_OK};
    WorkerPayload work_payload = {&record, 1, &result_payload, SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &work_payload, &record, NULL};
    size_t ran = 0U;

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0)
    {
        return 20;
    }

    if (expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_OK) != 0)
    {
        return 21;
    }

    if (record.run_count != 1U || record.count != 0U || sl_loop_pending_count(&loop) != 1U) {
        return 22;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.kinds[0] != SL_WORK_KIND_TEST ||
        record.statuses[0] != SL_STATUS_OK || record.results[0] != &result_payload ||
        record.users[0] != &record || record.values[0] != 42)
    {
        return 23;
    }

    return 0;
}

static int test_reinit_before_drain_is_rejected_and_preserves_completion(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[1];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    WorkerPayload result_payload = {&record, 77, NULL, SL_STATUS_OK};
    WorkerPayload work_payload = {&record, 1, &result_payload, SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &work_payload, &record, NULL};

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_OK) != 0)
    {
        return 24;
    }

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_INVALID_STATE) != 0 ||
        record.count != 0U || sl_loop_pending_count(&loop) != 1U)
    {
        return 25;
    }

    if (expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0 || record.count != 1U ||
        record.results[0] != &result_payload || record.values[0] != 77)
    {
        return 26;
    }

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0) {
        return 27;
    }

    return 0;
}

static int test_reset_after_loop_reset_destroys_pending_result(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[1];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    DestroyUser destroy_user = {&record, NULL};
    WorkerPayload result_payload = {&record, 88, NULL, SL_STATUS_OK};
    WorkerPayload work_payload = {&record, 1, &result_payload, SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &work_payload, &destroy_user,
                       destroy_result};

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_OK) != 0)
    {
        return 28;
    }

    sl_loop_reset(&loop);
    sl_worker_pool_reset_inline(&pool);

    if (record.destroy_count != 1U || destroy_user.destroyed_result != &result_payload ||
        record.count != 0U || sl_loop_pending_count(&loop) != 0U)
    {
        return 29;
    }

    if (expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0 || record.count != 1U ||
        record.destroy_count != 1U)
    {
        return 30;
    }

    return 0;
}

static int test_multiple_work_items_are_fifo(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[3];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    WorkerPayload first_result = {&record, 10, NULL, SL_STATUS_OK};
    WorkerPayload second_result = {&record, 20, NULL, SL_STATUS_OK};
    WorkerPayload first_payload = {&record, 1, &first_result, SL_STATUS_OK};
    WorkerPayload second_payload = {&record, 2, &second_result, SL_STATUS_OK};
    SlWorkItem first = {SL_WORK_KIND_BLOCKING_IO, run_recording_work, &first_payload, &record,
                        NULL};
    SlWorkItem second = {SL_WORK_KIND_CPU, run_recording_work, &second_payload, &record, NULL};

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 3U), SL_STATUS_OK) != 0)
    {
        return 30;
    }

    if (expect_status(sl_worker_pool_submit(&pool, &loop, &first, record_completion, &record),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, &loop, &second, record_completion, &record),
                      SL_STATUS_OK) != 0 ||
        record.count != 0U || record.run_count != 2U)
    {
        return 31;
    }

    if (expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0 || record.count != 2U ||
        record.kinds[0] != SL_WORK_KIND_BLOCKING_IO || record.values[0] != 10 ||
        record.kinds[1] != SL_WORK_KIND_CPU || record.values[1] != 20)
    {
        return 32;
    }

    return 0;
}

static int test_work_failure_is_posted(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[1];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    WorkerPayload work_payload = {&record, 1, NULL, SL_STATUS_INTERNAL};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &work_payload, &record, NULL};

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0)
    {
        return 40;
    }

    if (expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0)
    {
        return 41;
    }

    if (record.count != 1U || record.statuses[0] != SL_STATUS_INTERNAL || record.results[0] != NULL)
    {
        return 42;
    }

    return 0;
}

static int test_post_failure_destroys_result(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[1];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    DestroyUser destroy_user = {&record, NULL};
    WorkerPayload result_payload = {&record, 99, NULL, SL_STATUS_OK};
    WorkerPayload work_payload = {&record, 1, &result_payload, SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &work_payload, &destroy_user,
                       destroy_result};

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, noop_loop_completion, NULL, NULL),
            SL_STATUS_OK) != 0)
    {
        return 50;
    }

    if (expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 51;
    }

    if (record.destroy_count != 1U || destroy_user.destroyed_result != &result_payload ||
        record.count != 0U || sl_loop_pending_count(&loop) != 1U)
    {
        return 52;
    }

    return 0;
}

static int test_inline_record_capacity_exhaustion(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY + 1U];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    WorkerPayload work_payload = {&record, 1, NULL, SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &work_payload, &record, NULL};
    size_t index = 0U;

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY + 1U),
                      SL_STATUS_OK) != 0)
    {
        return 53;
    }

    for (index = 0U; index < SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY; index += 1U) {
        if (expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                          SL_STATUS_OK) != 0)
        {
            return 54;
        }
    }

    if (expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        record.run_count != SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY || record.count != 0U ||
        sl_loop_pending_count(&loop) != SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY)
    {
        return 55;
    }

    if (expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0 ||
        record.count != SL_WORKER_POOL_INLINE_COMPLETION_CAPACITY)
    {
        return 56;
    }

    return 0;
}

static int test_completion_failure_propagates_without_destroy(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[1];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_INTERNAL};
    DestroyUser destroy_user = {&record, NULL};
    WorkerPayload result_payload = {&record, 55, NULL, SL_STATUS_OK};
    WorkerPayload work_payload = {&record, 1, &result_payload, SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_recording_work, &work_payload, &destroy_user,
                       destroy_result};
    size_t ran = 0U;

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_worker_pool_submit(&pool, &loop, &item, record_completion, &record),
                      SL_STATUS_OK) != 0)
    {
        return 60;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_INTERNAL) != 0 || ran != 1U ||
        record.count != 1U || record.destroy_count != 0U || record.results[0] != &result_payload)
    {
        return 61;
    }

    return 0;
}

static int test_null_payload_and_user_are_allowed(void)
{
    SlWorkerPool pool = {0};
    SlCompletion storage[1];
    SlLoop loop;
    WorkerRecord record = {{SL_WORK_KIND_NONE}, {SL_STATUS_OK}, {NULL}, {NULL}, {0}, 0U, 0U, 0U,
                           SL_STATUS_OK};
    SlWorkItem item = {SL_WORK_KIND_TEST, run_null_payload_work, NULL, &record, NULL};
    size_t ran = 0U;

    if (expect_status(sl_worker_pool_init_inline(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0)
    {
        return 70;
    }

    if (expect_status(sl_worker_pool_submit(&pool, &loop, &item, expect_null_completion_user, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U)
    {
        return 71;
    }

    if (record.run_count != 1U || record.count != 0U) {
        return 72;
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_initialization();
    if (result != 0) {
        return result;
    }

    result = test_submit_validation();
    if (result != 0) {
        return result;
    }

    result = test_inline_execution_posts_completion();
    if (result != 0) {
        return result;
    }

    result = test_reinit_before_drain_is_rejected_and_preserves_completion();
    if (result != 0) {
        return result;
    }

    result = test_reset_after_loop_reset_destroys_pending_result();
    if (result != 0) {
        return result;
    }

    result = test_multiple_work_items_are_fifo();
    if (result != 0) {
        return result;
    }

    result = test_work_failure_is_posted();
    if (result != 0) {
        return result;
    }

    result = test_post_failure_destroys_result();
    if (result != 0) {
        return result;
    }

    result = test_inline_record_capacity_exhaustion();
    if (result != 0) {
        return result;
    }

    result = test_completion_failure_propagates_without_destroy();
    if (result != 0) {
        return result;
    }

    return test_null_payload_and_user_are_allowed();
}
