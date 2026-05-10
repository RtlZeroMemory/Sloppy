#include "sloppy/loop.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct LoopRecord
{
    int values[12];
    SlCompletionKind kinds[12];
    void* payloads[12];
    void* users[12];
    size_t count;
} LoopRecord;

typedef struct LoopPayload
{
    LoopRecord* record;
    int value;
} LoopPayload;

typedef struct PostPayload
{
    LoopPayload* next_payload;
    void* next_user;
    SlStatusCode expected_post_status;
    SlStatusCode actual_post_status;
} PostPayload;

typedef struct StopPayload
{
    LoopRecord* record;
    int value;
} StopPayload;

typedef struct ResetPostPayload
{
    LoopPayload* posted_payload;
    LoopRecord* record;
    SlStatus nested_status;
    SlStatus post_status;
} ResetPostPayload;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlStatus record_completion(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    LoopPayload* loop_payload = (LoopPayload*)payload;
    LoopRecord* record = loop_payload == NULL ? NULL : loop_payload->record;
    size_t index = 0U;

    (void)loop;
    if (record == NULL || record->count >= 12U) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->count;
    record->values[index] = loop_payload->value;
    record->kinds[index] = kind;
    record->payloads[index] = payload;
    record->users[index] = user;
    record->count += 1U;

    return sl_status_ok();
}

static SlStatus record_null_payload(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    LoopRecord* record = (LoopRecord*)user;
    size_t index = 0U;

    (void)loop;
    if (payload != NULL || record == NULL || record->count >= 12U) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->count;
    record->values[index] = -1;
    record->kinds[index] = kind;
    record->payloads[index] = payload;
    record->users[index] = user;
    record->count += 1U;

    return sl_status_ok();
}

static SlStatus failing_completion(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    (void)loop;
    (void)kind;
    (void)payload;
    (void)user;
    return sl_status_from_code(SL_STATUS_INTERNAL);
}

static SlStatus post_next_completion(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    PostPayload* post_payload = (PostPayload*)payload;
    SlStatus status;

    (void)kind;
    status = sl_loop_post(loop, SL_COMPLETION_KIND_TEST, record_completion,
                          post_payload == NULL ? NULL : post_payload->next_payload,
                          post_payload == NULL ? NULL : post_payload->next_user);
    if (post_payload != NULL) {
        post_payload->actual_post_status = sl_status_code(status);
        if (post_payload->actual_post_status != post_payload->expected_post_status) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
    }

    (void)user;
    return status;
}

static SlStatus stop_completion(SlLoop* loop, SlCompletionKind kind, void* payload, void* user)
{
    StopPayload* stop_payload = (StopPayload*)payload;
    LoopPayload loop_payload;

    (void)kind;
    (void)user;
    if (stop_payload != NULL) {
        SlStatus status;
        loop_payload.record = stop_payload->record;
        loop_payload.value = stop_payload->value;
        status = record_completion(loop, SL_COMPLETION_KIND_TEST, &loop_payload, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    sl_loop_stop(loop);
    return sl_status_ok();
}

static SlStatus nested_drain_completion(SlLoop* loop, SlCompletionKind kind, void* payload,
                                        void* user)
{
    SlStatus* out_status = (SlStatus*)user;

    (void)kind;
    (void)payload;
    if (out_status == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    *out_status = sl_loop_drain(loop, NULL);
    return sl_status_ok();
}

static SlStatus reset_then_nested_drain_completion(SlLoop* loop, SlCompletionKind kind,
                                                   void* payload, void* user)
{
    SlStatus* out_status = (SlStatus*)user;

    (void)kind;
    (void)payload;
    if (out_status == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    sl_loop_reset(loop);
    *out_status = sl_loop_drain(loop, NULL);
    return sl_status_ok();
}

static SlStatus reset_and_post_completion(SlLoop* loop, SlCompletionKind kind, void* payload,
                                          void* user)
{
    ResetPostPayload* reset_payload = (ResetPostPayload*)payload;

    (void)kind;
    (void)user;
    if (reset_payload == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    sl_loop_reset(loop);
    reset_payload->nested_status = sl_loop_drain(loop, NULL);
    reset_payload->post_status = sl_loop_post(loop, SL_COMPLETION_KIND_TEST, record_completion,
                                              reset_payload->posted_payload, reset_payload->record);
    return sl_status_ok();
}

static int test_initialization(void)
{
    SlCompletion storage[2];
    SlLoop loop;
    SlLoop zero;
    size_t ran = 99U;

    if (expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0) {
        return 1;
    }

    if (sl_loop_pending_count(&loop) != 0U || sl_loop_capacity(&loop) != 2U ||
        sl_loop_is_stopped(&loop))
    {
        return 2;
    }

    if (expect_status(sl_loop_init(NULL, storage, 2U), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 3;
    }

    if (expect_status(sl_loop_init(&loop, NULL, 2U), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 4;
    }

    if (expect_status(sl_loop_init(&zero, NULL, 0U), SL_STATUS_OK) != 0 ||
        sl_loop_capacity(&zero) != 0U ||
        expect_status(sl_loop_post(&zero, SL_COMPLETION_KIND_TEST, record_completion, NULL, NULL),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 5;
    }

    if (expect_status(sl_loop_drain(&zero, &ran), SL_STATUS_OK) != 0 || ran != 0U) {
        return 6;
    }

    if (sl_loop_pending_count(NULL) != 0U || sl_loop_capacity(NULL) != 0U ||
        sl_loop_is_stopped(NULL))
    {
        return 7;
    }

    return 0;
}

static int test_posting_and_failure_atomicity(void)
{
    SlCompletion storage[2];
    SlLoop loop;
    LoopRecord record = {{0}, {SL_COMPLETION_KIND_NONE}, {NULL}, {NULL}, 0U};
    LoopPayload first = {&record, 1};
    LoopPayload second = {&record, 2};

    if (expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0) {
        return 10;
    }

    if (expect_status(sl_loop_post(NULL, SL_COMPLETION_KIND_TEST, record_completion, &first, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, NULL, &first, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        sl_loop_pending_count(&loop) != 0U)
    {
        return 11;
    }

    if (expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &first, &record),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_USER, record_completion, &second, NULL),
            SL_STATUS_OK) != 0 ||
        sl_loop_pending_count(&loop) != 2U)
    {
        return 12;
    }

    if (expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &first, NULL),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sl_loop_pending_count(&loop) != 2U)
    {
        return 13;
    }

    if (expect_status(sl_loop_drain(&loop, NULL), SL_STATUS_OK) != 0 || record.count != 2U ||
        record.values[0] != 1 || record.values[1] != 2 ||
        record.kinds[0] != SL_COMPLETION_KIND_TEST || record.kinds[1] != SL_COMPLETION_KIND_USER ||
        record.payloads[0] != &first || record.users[0] != &record || record.users[1] != NULL)
    {
        return 14;
    }

    return 0;
}

static int test_run_once_fifo_and_null_payload(void)
{
    SlCompletion storage[3];
    SlLoop loop;
    LoopRecord record = {{0}, {SL_COMPLETION_KIND_NONE}, {NULL}, {NULL}, 0U};
    LoopPayload first = {&record, 10};
    LoopPayload second = {&record, 20};
    size_t ran = 99U;

    if (expect_status(sl_loop_init(&loop, storage, 3U), SL_STATUS_OK) != 0) {
        return 20;
    }

    if (expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_USER, record_completion, &first, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_null_payload, NULL, &record),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &second, NULL),
            SL_STATUS_OK) != 0)
    {
        return 21;
    }

    if (expect_status(sl_loop_run_once(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 10 || sl_loop_pending_count(&loop) != 2U)
    {
        return 22;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 2U ||
        record.count != 3U || record.values[1] != -1 || record.values[2] != 20)
    {
        return 23;
    }

    if (expect_status(sl_loop_run_once(NULL, &ran), SL_STATUS_INVALID_ARGUMENT) != 0 || ran != 0U) {
        return 24;
    }

    return 0;
}

static int test_drain_callback_can_post(void)
{
    SlCompletion storage[2];
    SlLoop loop;
    LoopRecord record = {{0}, {SL_COMPLETION_KIND_NONE}, {NULL}, {NULL}, 0U};
    LoopPayload posted = {&record, 42};
    PostPayload post_payload = {&posted, &record, SL_STATUS_OK, SL_STATUS_INTERNAL};
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0) {
        return 30;
    }

    if (expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, post_next_completion, &post_payload, NULL),
            SL_STATUS_OK) != 0)
    {
        return 31;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 2U ||
        record.count != 1U || record.values[0] != 42 ||
        post_payload.actual_post_status != SL_STATUS_OK)
    {
        return 32;
    }

    return 0;
}

static int test_stop_reset_and_post_after_stop(void)
{
    SlCompletion storage[3];
    SlLoop loop;
    LoopRecord record = {{0}, {SL_COMPLETION_KIND_NONE}, {NULL}, {NULL}, 0U};
    StopPayload stop_payload = {&record, 7};
    LoopPayload after_stop = {&record, 8};
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 3U), SL_STATUS_OK) != 0) {
        return 40;
    }

    if (expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, stop_completion, &stop_payload, NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &after_stop, NULL),
            SL_STATUS_OK) != 0)
    {
        return 41;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        !sl_loop_is_stopped(&loop) || sl_loop_pending_count(&loop) != 1U || record.count != 1U ||
        record.values[0] != 7)
    {
        return 42;
    }

    if (expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &after_stop, NULL),
            SL_STATUS_INVALID_STATE) != 0)
    {
        return 43;
    }

    if (expect_status(sl_loop_run_once(&loop, &ran), SL_STATUS_OK) != 0 || ran != 0U ||
        sl_loop_pending_count(&loop) != 1U)
    {
        return 44;
    }

    sl_loop_reset(&loop);
    if (sl_loop_is_stopped(&loop) || sl_loop_pending_count(&loop) != 0U ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &after_stop, NULL),
            SL_STATUS_OK) != 0)
    {
        return 45;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 2U || record.values[1] != 8)
    {
        return 46;
    }

    sl_loop_stop(NULL);
    sl_loop_reset(NULL);
    return 0;
}

static int test_callback_failure_and_nested_drain(void)
{
    SlCompletion storage[3];
    SlLoop loop;
    LoopRecord record = {{0}, {SL_COMPLETION_KIND_NONE}, {NULL}, {NULL}, 0U};
    LoopPayload after_failure = {&record, 9};
    SlStatus nested_status = sl_status_ok();
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 3U), SL_STATUS_OK) != 0) {
        return 50;
    }

    if (expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, failing_completion, NULL, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &after_failure, NULL),
            SL_STATUS_OK) != 0)
    {
        return 51;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_INTERNAL) != 0 || ran != 1U ||
        sl_loop_pending_count(&loop) != 1U)
    {
        return 52;
    }

    if (expect_status(sl_loop_run_once(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 9)
    {
        return 53;
    }

    if (expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, nested_drain_completion, NULL,
                                   &nested_status),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        sl_status_code(nested_status) != SL_STATUS_INVALID_STATE)
    {
        return 54;
    }

    return 0;
}

static int test_reset_preserves_active_drain_guard(void)
{
    SlCompletion storage[2];
    SlLoop loop;
    SlStatus nested_status = sl_status_ok();
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 2U), SL_STATUS_OK) != 0) {
        return 60;
    }

    if (expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_TEST,
                                   reset_then_nested_drain_completion, NULL, &nested_status),
                      SL_STATUS_OK) != 0)
    {
        return 61;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        sl_status_code(nested_status) != SL_STATUS_INVALID_STATE ||
        sl_loop_pending_count(&loop) != 0U)
    {
        return 62;
    }

    if (expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, failing_completion, NULL, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 63;
    }

    return 0;
}

static int test_reset_inside_callback_discards_pending_and_continues_new_posts(void)
{
    SlCompletion storage[3];
    SlLoop loop;
    LoopRecord record = {{0}, {SL_COMPLETION_KIND_NONE}, {NULL}, {NULL}, 0U};
    LoopPayload discarded = {&record, 100};
    LoopPayload posted = {&record, 200};
    ResetPostPayload reset_payload = {&posted, &record, sl_status_ok(), sl_status_ok()};
    size_t ran = 0U;

    if (expect_status(sl_loop_init(&loop, storage, 3U), SL_STATUS_OK) != 0) {
        return 70;
    }

    if (expect_status(sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, reset_and_post_completion,
                                   &reset_payload, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_loop_post(&loop, SL_COMPLETION_KIND_TEST, record_completion, &discarded, NULL),
            SL_STATUS_OK) != 0)
    {
        return 71;
    }

    if (expect_status(sl_loop_drain(&loop, &ran), SL_STATUS_OK) != 0 || ran != 2U ||
        record.count != 1U || record.values[0] != 200 ||
        sl_status_code(reset_payload.nested_status) != SL_STATUS_INVALID_STATE ||
        sl_status_code(reset_payload.post_status) != SL_STATUS_OK ||
        sl_loop_pending_count(&loop) != 0U || sl_loop_is_stopped(&loop))
    {
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

    result = test_posting_and_failure_atomicity();
    if (result != 0) {
        return result;
    }

    result = test_run_once_fifo_and_null_payload();
    if (result != 0) {
        return result;
    }

    result = test_drain_callback_can_post();
    if (result != 0) {
        return result;
    }

    result = test_stop_reset_and_post_after_stop();
    if (result != 0) {
        return result;
    }

    result = test_callback_failure_and_nested_drain();
    if (result != 0) {
        return result;
    }

    result = test_reset_preserves_active_drain_guard();
    if (result != 0) {
        return result;
    }

    return test_reset_inside_callback_discards_pending_and_continues_new_posts();
}
