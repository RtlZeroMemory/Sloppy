#include "sloppy/async.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct AsyncRecord
{
    SlAsyncState states[8];
    SlStatusCode statuses[8];
    const SlDiag* diags[8];
    void* payloads[8];
    void* result_users[8];
    void* continuation_users[8];
    size_t count;
    SlStatusCode return_code;
} AsyncRecord;

typedef struct AsyncOrderPayload
{
    int* out_slot;
    int value;
} AsyncOrderPayload;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlStatus record_async(SlLoop* loop, SlAsyncState state, const SlAsyncResult* result,
                             void* user)
{
    AsyncRecord* record = (AsyncRecord*)user;
    size_t index = 0U;

    (void)loop;
    if (record == NULL || result == NULL || record->count >= 8U) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->count;
    record->states[index] = state;
    record->statuses[index] = sl_status_code(result->status);
    record->diags[index] = result->diag;
    record->payloads[index] = result->payload;
    record->result_users[index] = result->user;
    record->continuation_users[index] = user;
    record->count += 1U;

    return sl_status_from_code(record->return_code);
}

static SlStatus record_order(SlLoop* loop, SlAsyncState state, const SlAsyncResult* result,
                             void* user)
{
    AsyncRecord* record = (AsyncRecord*)user;
    AsyncOrderPayload* order_payload = result == NULL ? NULL : (AsyncOrderPayload*)result->payload;

    (void)loop;
    if (state != SL_ASYNC_STATE_FULFILLED || record == NULL || result == NULL ||
        order_payload == NULL || order_payload->out_slot == NULL)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    *order_payload->out_slot = order_payload->value;
    record->count += 1U;
    return sl_status_ok();
}

static SlStatus noop_completion(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    (void)loop;
    (void)kind;
    (void)payload;
    (void)user;
    return sl_status_ok();
}

static int test_initialization(void)
{
    SlAsync async = {0};
    AsyncRecord record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};

    if (expect_status(sl_async_init(&async, record_async, &record), SL_STATUS_OK) != 0) {
        return 1;
    }

    if (sl_async_state(&async) != SL_ASYNC_STATE_PENDING || !sl_async_is_pending(&async) ||
        sl_async_is_settled(&async) || async.has_result)
    {
        return 2;
    }

    if (expect_status(sl_async_init(NULL, record_async, &record), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        expect_status(sl_async_init(&async, NULL, &record), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 3;
    }

    if (sl_async_state(NULL) != SL_ASYNC_STATE_PENDING || sl_async_is_pending(NULL) ||
        sl_async_is_settled(NULL))
    {
        return 4;
    }

    return 0;
}

static int test_fulfill_posts_and_drains(void)
{
    SlCompletion storage[2];
    SlLoop loop;
    SlAsync async = {0};
    AsyncRecord record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};
    int payload = 42;
    int result_user = 7;
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&async, record_async, &record), SL_STATUS_OK) != 0)
    {
        return 10;
    }

    if (expect_status(sl_async_fulfill(&async, &loop, &payload, &result_user), SL_STATUS_OK) != 0 ||
        !sl_async_is_settled(&async) || sl_async_state(&async) != SL_ASYNC_STATE_FULFILLED ||
        sl_loop_pending_count(&loop) != 1U || record.count != 0U)
    {
        return 11;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.states[0] != SL_ASYNC_STATE_FULFILLED ||
        record.statuses[0] != SL_STATUS_OK || record.diags[0] != NULL ||
        record.payloads[0] != &payload || record.result_users[0] != &result_user ||
        record.continuation_users[0] != &record)
    {
        return 12;
    }

    if (expect_status(sl_async_fulfill(&async, &loop, NULL, NULL), SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_async_reject(&async, &loop, sl_status_from_code(SL_STATUS_INTERNAL), NULL),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 13;
    }

    return 0;
}

static int test_reject_posts_diag(void)
{
    SlCompletion storage[2];
    SlLoop loop;
    SlAsync async = {0};
    SlDiag diag;
    AsyncRecord record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};
    size_t ran = 0U;

    diag = (SlDiag){0};

    if (expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&async, record_async, &record), SL_STATUS_OK) != 0)
    {
        return 20;
    }

    if (expect_status(sl_async_reject(&async, &loop, sl_status_ok(), &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !sl_async_is_pending(&async))
    {
        return 21;
    }

    if (expect_status(
            sl_async_reject(&async, &loop, sl_status_from_code(SL_STATUS_INTERNAL), &diag),
            SL_STATUS_OK) != 0 ||
        sl_async_state(&async) != SL_ASYNC_STATE_REJECTED)
    {
        return 22;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.states[0] != SL_ASYNC_STATE_REJECTED ||
        record.statuses[0] != SL_STATUS_INTERNAL || record.diags[0] != &diag ||
        record.payloads[0] != NULL || record.result_users[0] != NULL)
    {
        return 23;
    }

    if (expect_status(
            sl_async_reject(&async, &loop, sl_status_from_code(SL_STATUS_INTERNAL), &diag),
            SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_async_fulfill(&async, &loop, NULL, NULL), SL_STATUS_INVALID_STATE) != 0)
    {
        return 24;
    }

    return 0;
}

static int test_cancel_posts_diag(void)
{
    SlCompletion storage[1];
    SlLoop loop;
    SlAsync async = {0};
    SlDiag diag;
    AsyncRecord record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};

    diag = (SlDiag){0};

    if (expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&async, record_async, &record), SL_STATUS_OK) != 0)
    {
        return 30;
    }

    if (expect_status(sl_async_cancel(&async, &loop, sl_status_ok(), &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(
            sl_async_cancel(&async, &loop, sl_status_from_code(SL_STATUS_INVALID_STATE), &diag),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0 || record.count != 1U ||
        record.states[0] != SL_ASYNC_STATE_CANCELLED ||
        record.statuses[0] != SL_STATUS_INVALID_STATE || record.diags[0] != &diag)
    {
        return 31;
    }

    return 0;
}

static int test_loop_post_failure_leaves_pending(void)
{
    SlCompletion storage[1];
    SlLoop full_loop;
    SlLoop zero_loop;
    SlAsync async = {0};
    AsyncRecord record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};

    if (expect_status(sl_loop_init(&zero_loop, NULL, 0U), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&async, record_async, &record), SL_STATUS_OK) != 0)
    {
        return 40;
    }

    if (expect_status(sl_async_fulfill(&async, &zero_loop, NULL, NULL),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        !sl_async_is_pending(&async) || async.has_result)
    {
        return 41;
    }

    if (expect_status(sl_loop_init(&full_loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&full_loop, SL_COMPLETION_KIND_TEST, noop_completion, NULL, NULL),
            SL_STATUS_OK) != 0)
    {
        return 42;
    }

    if (expect_status(sl_async_fulfill(&async, &full_loop, NULL, NULL),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        !sl_async_is_pending(&async) || async.has_result)
    {
        return 43;
    }

    if (expect_status(sl_async_fulfill(NULL, &full_loop, NULL, NULL), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        expect_status(sl_async_fulfill(&async, NULL, NULL, NULL), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 44;
    }

    return 0;
}

static int test_continuation_failure_propagates(void)
{
    SlCompletion storage[1];
    SlLoop loop;
    SlAsync async = {0};
    AsyncRecord record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U,
        SL_STATUS_INTERNAL};
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&async, record_async, &record), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_fulfill(&async, &loop, NULL, NULL), SL_STATUS_OK) != 0)
    {
        return 50;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_INTERNAL) != 0 || ran != 1U ||
        record.count != 1U || sl_loop_pending_count(&loop) != 0U)
    {
        return 51;
    }

    return 0;
}

static int test_reinit_before_drain_is_rejected_and_preserves_completion(void)
{
    SlCompletion storage[1];
    SlLoop loop;
    SlAsync async = {0};
    AsyncRecord original_record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};
    AsyncRecord replacement_record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};
    int payload = 123;
    int result_user = 456;
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&async, record_async, &original_record), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_fulfill(&async, &loop, &payload, &result_user), SL_STATUS_OK) != 0)
    {
        return 60;
    }

    if (expect_status(sl_async_init(&async, record_async, &replacement_record),
                      SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_async_fulfill(&async, &loop, NULL, NULL), SL_STATUS_INVALID_STATE) != 0 ||
        original_record.count != 0U || replacement_record.count != 0U)
    {
        return 61;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        original_record.count != 1U || replacement_record.count != 0U ||
        original_record.states[0] != SL_ASYNC_STATE_FULFILLED ||
        original_record.payloads[0] != &payload || original_record.result_users[0] != &result_user)
    {
        return 62;
    }

    if (expect_status(sl_async_init(&async, record_async, &replacement_record), SL_STATUS_OK) !=
            0 ||
        !sl_async_is_pending(&async) || async.has_result || async.completion_posted)
    {
        return 63;
    }

    return 0;
}

static int test_completion_order(void)
{
    SlCompletion storage[2];
    SlLoop loop;
    SlAsync first = {0};
    SlAsync second = {0};
    AsyncRecord record = {
        {SL_ASYNC_STATE_PENDING}, {SL_STATUS_OK}, {NULL}, {NULL}, {NULL}, {NULL}, 0U, SL_STATUS_OK};
    int observed[2] = {0, 0};
    AsyncOrderPayload first_payload = {&observed[0], 11};
    AsyncOrderPayload second_payload = {&observed[1], 22};

    if (expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&first, record_order, &record), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_init(&second, record_order, &record), SL_STATUS_OK) != 0)
    {
        return 70;
    }

    if (expect_status(sl_async_fulfill(&first, &loop, &first_payload, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_fulfill(&second, &loop, &second_payload, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0)
    {
        return 71;
    }

    if (record.count != 2U || observed[0] != 11 || observed[1] != 22) {
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

    result = test_fulfill_posts_and_drains();
    if (result != 0) {
        return result;
    }

    result = test_reject_posts_diag();
    if (result != 0) {
        return result;
    }

    result = test_cancel_posts_diag();
    if (result != 0) {
        return result;
    }

    result = test_loop_post_failure_leaves_pending();
    if (result != 0) {
        return result;
    }

    result = test_continuation_failure_propagates();
    if (result != 0) {
        return result;
    }

    result = test_reinit_before_drain_is_rejected_and_preserves_completion();
    if (result != 0) {
        return result;
    }

    return test_completion_order();
}
