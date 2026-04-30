#include "sloppy/provider_executor.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct ProviderRecord
{
    SlStatusCode statuses[16];
    SlDiagCode diag_codes[16];
    SlAsyncOperationKind operation_kinds[16];
    SlStr messages[16];
    int worker_values[16];
    size_t worker_count;
    size_t dispatch_count;
    size_t cleanup_count;
} ProviderRecord;

typedef struct ProviderWorkPayload
{
    ProviderRecord* record;
    int value;
    SlStatusCode status_code;
    SlDiagCode diag_code;
    SlStr message;
    bool cancel_before_success;
} ProviderWorkPayload;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlStatus record_provider_completion(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                           void* user)
{
    ProviderRecord* record = (ProviderRecord*)user;
    size_t index = 0U;

    (void)loop;
    if (record == NULL || completion == NULL || completion->operation == NULL ||
        record->dispatch_count >= 16U)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->dispatch_count;
    record->statuses[index] = sl_status_code(completion->status);
    record->diag_codes[index] = completion->diag == NULL ? SL_DIAG_NONE : completion->diag->code;
    record->messages[index] = completion->diag == NULL ? sl_str_empty() : completion->diag->message;
    record->operation_kinds[index] = completion->operation_kind;
    record->dispatch_count += 1U;
    return sl_status_ok();
}

static SlStatus noop_async_completion(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                      void* user)
{
    (void)loop;
    (void)completion;
    (void)user;
    return sl_status_ok();
}

static void cleanup_provider_operation(SlProviderOperation* operation, void* user)
{
    ProviderRecord* record = (ProviderRecord*)user;

    (void)operation;
    if (record != NULL) {
        record->cleanup_count += 1U;
    }
}

static SlStatus run_provider_like_operation(SlProviderOperation* operation, void* user,
                                            SlDiagCode* out_diag_code, SlStr* out_message)
{
    ProviderWorkPayload* payload = (ProviderWorkPayload*)user;
    ProviderRecord* record = payload == NULL ? NULL : payload->record;
    size_t index = 0U;

    if (operation == NULL || payload == NULL || record == NULL || record->worker_count >= 16U) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->worker_count;
    record->worker_values[index] = payload->value;
    record->worker_count += 1U;
    if (payload->cancel_before_success) {
        (void)sl_provider_operation_cancel(operation, sl_str_from_cstr("worker cancelled"));
    }
    if (out_diag_code != NULL) {
        *out_diag_code = payload->diag_code;
    }
    if (out_message != NULL) {
        *out_message = payload->message;
    }
    return sl_status_from_code(payload->status_code);
}

static int make_descriptor_for(ProviderRecord* record, SlBytes input, SlCancellationToken* token,
                               SlStr instance_id, SlStr provider_kind, SlProviderExecutionMode mode,
                               SlProviderOperationDescriptor* out_descriptor)
{
    SlProviderOperationDescriptor desc = sl_provider_operation_descriptor_init(
        instance_id, provider_kind, SL_PROVIDER_OPERATION_KIND_QUERY, sl_str_from_cstr("query"),
        mode, record_provider_completion, record);

    if (out_descriptor == NULL) {
        return 1;
    }
    if (expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc, sl_str_from_cstr("data.main"), SL_CAPABILITY_OPERATION_READ),
                      SL_STATUS_OK) != 0)
    {
        return 2;
    }
    if (expect_status(sl_provider_operation_descriptor_attach_cancellation(&desc, token, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 3;
    }
    if (expect_status(sl_provider_operation_descriptor_set_input(&desc, input), SL_STATUS_OK) != 0)
    {
        return 4;
    }
    if (expect_status(sl_provider_operation_descriptor_set_diagnostic_context(
                          &desc, sl_str_from_cstr("route users#index")),
                      SL_STATUS_OK) != 0)
    {
        return 5;
    }
    if (expect_status(sl_provider_operation_descriptor_attach_cleanup(
                          &desc, cleanup_provider_operation, record),
                      SL_STATUS_OK) != 0)
    {
        return 6;
    }

    *out_descriptor = desc;
    return 0;
}

static int make_descriptor(ProviderRecord* record, SlBytes input, SlCancellationToken* token,
                           SlProviderOperationDescriptor* out_descriptor)
{
    return make_descriptor_for(record, input, token, sl_str_from_cstr("sqlite:main"),
                               sl_str_from_cstr("sqlite"),
                               SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING, out_descriptor);
}

static int make_worker_descriptor(ProviderRecord* record, ProviderWorkPayload* payload,
                                  SlProviderOperationDescriptor* out_descriptor)
{
    if (make_descriptor(record, sl_bytes_empty(), NULL, out_descriptor) != 0) {
        return 1;
    }
    return expect_status(sl_provider_operation_descriptor_attach_run(
                             out_descriptor, run_provider_like_operation, payload),
                         SL_STATUS_OK);
}

static int drain_until_dispatch_count(SlAsyncLoop* loop, ProviderRecord* record, size_t expected)
{
    size_t attempts = 0U;

    if (loop == NULL || record == NULL) {
        return 1;
    }

    for (attempts = 0U; attempts < 100000U; attempts += 1U) {
        size_t ran = 0U;
        if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0) {
            return 2;
        }
        if (record->dispatch_count >= expected) {
            return 0;
        }
    }

    return 3;
}

static int init_executor_with_backend(SlArena* arena, SlAsyncLoop** loop,
                                      SlProviderInstanceExecutor* executor,
                                      SlProviderExecutorSlot* slots, SlAsyncCompletion* completions,
                                      size_t capacity, SlAsyncBackendKind backend)
{
    SlProviderExecutorConfig config = {0};

    config.instance_id = sl_str_from_cstr("sqlite:main");
    config.provider_kind = sl_str_from_cstr("sqlite");
    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.queue_capacity = capacity;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    if (expect_status(sl_async_loop_create(backend, arena, completions, 16U, loop), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }
    return expect_status(sl_provider_executor_init(executor, arena, &config, slots, *loop),
                         SL_STATUS_OK);
}

static int init_executor(SlArena* arena, SlAsyncLoop** loop, SlProviderInstanceExecutor* executor,
                         SlProviderExecutorSlot* slots, SlAsyncCompletion* completions,
                         size_t capacity)
{
    return init_executor_with_backend(arena, loop, executor, slots, completions, capacity,
                                      SL_ASYNC_BACKEND_TEST);
}

static int test_execution_mode_parse_and_validation(void)
{
    SlProviderExecutionMode mode = SL_PROVIDER_EXECUTION_INLINE_FAST;

    if (expect_status(
            sl_provider_execution_mode_parse(sl_str_from_cstr("SerializedBlocking"), &mode),
            SL_STATUS_OK) != 0 ||
        mode != SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING ||
        !sl_str_equal(sl_provider_execution_mode_name(SL_PROVIDER_EXECUTION_NONBLOCKING_IO),
                      sl_str_from_cstr("NonBlockingIo")) ||
        expect_status(sl_provider_execution_mode_parse(sl_str_from_cstr("mystery"), &mode),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 1;
    }

    if (!sl_provider_operation_kind_is_supported(SL_PROVIDER_OPERATION_KIND_QUERY) ||
        sl_provider_operation_kind_is_supported(SL_PROVIDER_OPERATION_KIND_UNKNOWN) ||
        !sl_str_equal(sl_provider_operation_kind_name(SL_PROVIDER_OPERATION_KIND_QUERY_ONE),
                      sl_str_from_cstr("queryOne")))
    {
        return 2;
    }

    return 0;
}

static int test_descriptor_helpers_preserve_outputs_on_failure(void)
{
    ProviderRecord record = {0};
    SlProviderOperationDescriptor desc = sl_provider_operation_descriptor_init(
        sl_str_from_cstr("sqlite:main"), sl_str_from_cstr("sqlite"),
        SL_PROVIDER_OPERATION_KIND_QUERY, sl_str_from_cstr("query"),
        SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING, record_provider_completion, &record);
    SlBytes good = sl_bytes_from_parts((const unsigned char*)"select", 6U);
    SlBytes bad = {0};

    if (expect_status(sl_provider_operation_descriptor_set_input(&desc, good), SL_STATUS_OK) != 0 ||
        desc.input.ptr != good.ptr || desc.input.length != good.length)
    {
        return 60;
    }

    bad.ptr = NULL;
    bad.length = 4U;
    if (expect_status(sl_provider_operation_descriptor_set_input(&desc, bad),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        desc.input.ptr != good.ptr || desc.input.length != good.length ||
        expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc, sl_str_from_cstr("data.main"), SL_CAPABILITY_OPERATION_READ),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_set_diagnostic_context(
                          &desc, sl_str_from_cstr("safe context")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_cleanup(
                          &desc, cleanup_provider_operation, &record),
                      SL_STATUS_OK) != 0)
    {
        return 61;
    }

    return 0;
}

static int test_invalid_descriptor_fields_fail_without_cleanup(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[1];
    ProviderRecord record = {0};
    SlProviderOperation* op = (SlProviderOperation*)1;
    SlProviderOperationDescriptor desc;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor(&arena, &loop, &executor, slots, completions, 1U) != 0)
    {
        return 70;
    }

    if (make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0) {
        return 71;
    }
    desc.provider_instance_id = sl_str_empty();
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        op != NULL || record.cleanup_count != 0U)
    {
        return 72;
    }

    if (make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0) {
        return 73;
    }
    desc.execution_mode = SL_PROVIDER_EXECUTION_EXTERNAL_MANAGED;
    op = (SlProviderOperation*)1;
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        op != NULL || record.cleanup_count != 0U)
    {
        return 74;
    }

    if (make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0) {
        return 75;
    }
    desc.provider_kind = sl_str_from_cstr("postgres");
    op = (SlProviderOperation*)1;
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        op != NULL || record.cleanup_count != 0U)
    {
        return 76;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_serialized_admission_overflow_and_recovery(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    unsigned char first_bytes[] = {'s', 'e', 'l', '1'};
    unsigned char second_bytes[] = {'s', 'e', 'l', '2'};
    unsigned char third_bytes[] = {'s', 'e', 'l', '3'};
    SlProviderOperation* first = NULL;
    SlProviderOperation* second = NULL;
    SlProviderOperation* third = NULL;
    SlProviderOperationDescriptor first_desc;
    SlProviderOperationDescriptor second_desc;
    SlProviderOperationDescriptor third_desc;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor(&arena, &loop, &executor, slots, completions, 2U) != 0)
    {
        return 10;
    }

    if (make_descriptor(&record, sl_bytes_from_parts(first_bytes, sizeof(first_bytes)), NULL,
                        &first_desc) != 0 ||
        make_descriptor(&record, sl_bytes_from_parts(second_bytes, sizeof(second_bytes)), NULL,
                        &second_desc) != 0 ||
        make_descriptor(&record, sl_bytes_from_parts(third_bytes, sizeof(third_bytes)), NULL,
                        &third_desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &first_desc, &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &second_desc, &second),
                      SL_STATUS_OK) != 0 ||
        sl_provider_operation_state(first) != SL_PROVIDER_OPERATION_ACTIVE ||
        sl_provider_operation_state(second) != SL_PROVIDER_OPERATION_QUEUED ||
        sl_provider_executor_in_flight_count(&executor) != 1U ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &third_desc, &third),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        third != NULL || executor.overflow_count != 1U)
    {
        return 11;
    }

    first_bytes[0] = 'X';
    if (!sl_bytes_equal(sl_provider_operation_input(first),
                        sl_bytes_from_parts((const unsigned char*)"sel1", 4U)))
    {
        return 12;
    }

    if (expect_status(sl_provider_operation_complete(first, sl_status_ok(), SL_DIAG_NONE,
                                                     sl_str_from_cstr("ok")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.dispatch_count != 1U || record.cleanup_count != 1U ||
        record.operation_kinds[0] != SL_ASYNC_OPERATION_PROVIDER ||
        sl_provider_operation_state(second) != SL_PROVIDER_OPERATION_ACTIVE)
    {
        return 13;
    }

    if (expect_status(sl_provider_executor_submit(&executor, &arena, &third_desc, &third),
                      SL_STATUS_OK) != 0 ||
        sl_provider_operation_state(third) != SL_PROVIDER_OPERATION_QUEUED)
    {
        return 14;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_cancel_timeout_and_late_completion_cleanup_once(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    SlCancellationToken token;
    SlProviderOperation* cancelled = NULL;
    SlProviderOperation* timed_out = NULL;
    SlProviderOperationDescriptor cancel_desc;
    SlProviderOperationDescriptor timeout_desc;
    size_t ran = 0U;

    sl_cancellation_token_init(&token);
    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor(&arena, &loop, &executor, slots, completions, 2U) != 0)
    {
        return 20;
    }

    if (make_descriptor(&record, sl_bytes_empty(), &token, &cancel_desc) != 0 ||
        make_descriptor(&record, sl_bytes_empty(), NULL, &timeout_desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &cancel_desc, &cancelled),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &timeout_desc, &timed_out),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_cancel(cancelled, sl_str_from_cstr("request aborted")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_complete(cancelled, sl_status_ok(), SL_DIAG_NONE,
                                                     sl_str_from_cstr("late")),
                      SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.dispatch_count != 1U || record.cleanup_count != 1U ||
        record.statuses[0] != SL_STATUS_CANCELLED ||
        record.diag_codes[0] != SL_DIAG_ENGINE_CANCELLED ||
        !sl_cancellation_token_is_cancelled(&token))
    {
        return 21;
    }

    if (sl_provider_operation_state(timed_out) != SL_PROVIDER_OPERATION_ACTIVE ||
        expect_status(sl_provider_operation_timeout(timed_out, sl_str_from_cstr("deadline")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.dispatch_count != 2U || record.cleanup_count != 2U ||
        record.statuses[1] != SL_STATUS_DEADLINE_EXCEEDED ||
        record.diag_codes[1] != SL_DIAG_ENGINE_PROMISE_PENDING ||
        expect_status(sl_provider_operation_cancel(timed_out, sl_str_from_cstr("again")),
                      SL_STATUS_INVALID_STATE) != 0 ||
        record.cleanup_count != 2U)
    {
        return 22;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_shutdown_rejects_new_work_and_cancels_pending(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    SlProviderOperation* first = NULL;
    SlProviderOperation* second = NULL;
    SlProviderOperation* rejected = NULL;
    SlProviderOperationDescriptor desc;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor(&arena, &loop, &executor, slots, completions, 2U) != 0)
    {
        return 30;
    }

    if (make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &second),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_shutdown(&executor,
                                                    SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL),
                      SL_STATUS_OK) != 0 ||
        !sl_provider_executor_is_shutting_down(&executor) ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &rejected),
                      SL_STATUS_CANCELLED) != 0 ||
        rejected != NULL || expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 ||
        ran != 2U || record.dispatch_count != 2U || record.cleanup_count != 2U ||
        record.statuses[0] != SL_STATUS_CANCELLED || record.statuses[1] != SL_STATUS_CANCELLED ||
        record.diag_codes[0] != SL_DIAG_APP_LIFECYCLE ||
        record.diag_codes[1] != SL_DIAG_APP_LIFECYCLE ||
        sl_provider_executor_pending_count(&executor) != 0U)
    {
        return 31;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    (void)first;
    (void)second;
    return 0;
}

static int test_blocking_pool_promotes_queued_when_slot_frees(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderExecutorConfig config = {0};
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    SlProviderOperation* first = NULL;
    SlProviderOperation* second = NULL;
    SlProviderOperationDescriptor first_desc;
    SlProviderOperationDescriptor second_desc;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, completions, 16U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 80;
    }

    config.instance_id = sl_str_from_cstr("postgres:main");
    config.provider_kind = sl_str_from_cstr("postgres");
    config.mode = SL_PROVIDER_EXECUTION_BLOCKING_POOL;
    config.queue_capacity = 2U;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, slots, loop),
                      SL_STATUS_OK) != 0 ||
        make_descriptor_for(&record, sl_bytes_empty(), NULL, sl_str_from_cstr("postgres:main"),
                            sl_str_from_cstr("postgres"), SL_PROVIDER_EXECUTION_BLOCKING_POOL,
                            &first_desc) != 0 ||
        make_descriptor_for(&record, sl_bytes_empty(), NULL, sl_str_from_cstr("postgres:main"),
                            sl_str_from_cstr("postgres"), SL_PROVIDER_EXECUTION_BLOCKING_POOL,
                            &second_desc) != 0)
    {
        return 81;
    }

    if (expect_status(sl_provider_executor_submit(&executor, &arena, &first_desc, &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &second_desc, &second),
                      SL_STATUS_OK) != 0 ||
        sl_provider_operation_state(first) != SL_PROVIDER_OPERATION_ACTIVE ||
        sl_provider_operation_state(second) != SL_PROVIDER_OPERATION_QUEUED ||
        sl_provider_executor_in_flight_count(&executor) != 1U)
    {
        return 82;
    }

    if (expect_status(sl_provider_operation_complete(first, sl_status_ok(), SL_DIAG_NONE,
                                                     sl_str_from_cstr("first done")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        sl_provider_operation_state(second) != SL_PROVIDER_OPERATION_ACTIVE ||
        sl_provider_executor_in_flight_count(&executor) != 1U ||
        sl_provider_executor_pending_count(&executor) != 1U || record.cleanup_count != 1U)
    {
        return 83;
    }

    if (expect_status(sl_provider_operation_complete(second, sl_status_ok(), SL_DIAG_NONE,
                                                     sl_str_from_cstr("second done")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        sl_provider_executor_in_flight_count(&executor) != 0U ||
        sl_provider_executor_pending_count(&executor) != 0U || record.cleanup_count != 2U)
    {
        return 84;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_serialized_worker_executes_one_at_a_time_fifo(void)
{
    unsigned char arena_storage[16384];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[3];
    ProviderRecord record = {0};
    ProviderWorkPayload first_payload = {
        &record, 1, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("one"), false};
    ProviderWorkPayload second_payload = {
        &record, 2, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("two"), false};
    ProviderWorkPayload third_payload = {
        &record, 3, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("three"), false};
    SlProviderOperation* first = NULL;
    SlProviderOperation* second = NULL;
    SlProviderOperation* third = NULL;
    SlProviderOperationDescriptor first_desc;
    SlProviderOperationDescriptor second_desc;
    SlProviderOperationDescriptor third_desc;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor_with_backend(&arena, &loop, &executor, slots, completions, 3U,
                                   SL_ASYNC_BACKEND_LIBUV) != 0)
    {
        return 90;
    }

    if (make_worker_descriptor(&record, &first_payload, &first_desc) != 0 ||
        make_worker_descriptor(&record, &second_payload, &second_desc) != 0 ||
        make_worker_descriptor(&record, &third_payload, &third_desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &first_desc, &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &second_desc, &second),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &third_desc, &third),
                      SL_STATUS_OK) != 0 ||
        sl_provider_operation_state(first) != SL_PROVIDER_OPERATION_ACTIVE ||
        sl_provider_operation_state(second) != SL_PROVIDER_OPERATION_QUEUED ||
        sl_provider_operation_state(third) != SL_PROVIDER_OPERATION_QUEUED ||
        sl_provider_executor_in_flight_count(&executor) != 1U ||
        executor.worker_started_count != 1U)
    {
        return 91;
    }

    if (drain_until_dispatch_count(loop, &record, 3U) != 0 || record.worker_count != 3U ||
        record.dispatch_count != 3U || record.cleanup_count != 3U || record.worker_values[0] != 1 ||
        record.worker_values[1] != 2 || record.worker_values[2] != 3 ||
        record.statuses[0] != SL_STATUS_OK || record.statuses[1] != SL_STATUS_OK ||
        record.statuses[2] != SL_STATUS_OK || sl_provider_executor_pending_count(&executor) != 0U ||
        sl_provider_executor_in_flight_count(&executor) != 0U)
    {
        return 92;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_serialized_worker_capacity_and_reject_ownership(void)
{
    unsigned char arena_storage[16384];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[1];
    ProviderRecord record = {0};
    ProviderWorkPayload first_payload = {
        &record, 10, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("ten"), false};
    ProviderWorkPayload rejected_payload = {
        &record, 11, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("eleven"), false};
    SlProviderOperation* first = NULL;
    SlProviderOperation* rejected = NULL;
    SlProviderOperationDescriptor first_desc;
    SlProviderOperationDescriptor rejected_desc;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor_with_backend(&arena, &loop, &executor, slots, completions, 1U,
                                   SL_ASYNC_BACKEND_LIBUV) != 0)
    {
        return 100;
    }

    if (make_worker_descriptor(&record, &first_payload, &first_desc) != 0 ||
        make_worker_descriptor(&record, &rejected_payload, &rejected_desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &first_desc, &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &rejected_desc, &rejected),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        rejected != NULL || executor.overflow_count != 1U || record.cleanup_count != 0U)
    {
        return 101;
    }

    if (drain_until_dispatch_count(loop, &record, 1U) != 0 || record.worker_count != 1U ||
        record.worker_values[0] != 10 || record.dispatch_count != 1U || record.cleanup_count != 1U)
    {
        return 102;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_serialized_worker_failure_and_late_completion_cleanup_once(void)
{
    unsigned char arena_storage[16384];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    ProviderWorkPayload failure_payload = {&record,
                                           20,
                                           SL_STATUS_INTERNAL,
                                           SL_DIAG_SQLITE_PROVIDER_ERROR,
                                           sl_str_from_cstr("provider failure"),
                                           false};
    ProviderWorkPayload late_payload = {
        &record, 21, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("late success"), true};
    SlProviderOperation* failure = NULL;
    SlProviderOperation* late = NULL;
    SlProviderOperationDescriptor failure_desc;
    SlProviderOperationDescriptor late_desc;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor_with_backend(&arena, &loop, &executor, slots, completions, 2U,
                                   SL_ASYNC_BACKEND_LIBUV) != 0)
    {
        return 110;
    }

    if (make_worker_descriptor(&record, &failure_payload, &failure_desc) != 0 ||
        make_worker_descriptor(&record, &late_payload, &late_desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &failure_desc, &failure),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &late_desc, &late),
                      SL_STATUS_OK) != 0)
    {
        return 111;
    }

    if (drain_until_dispatch_count(loop, &record, 2U) != 0 || record.worker_count != 2U ||
        record.dispatch_count != 2U || record.cleanup_count != 2U ||
        record.statuses[0] != SL_STATUS_INTERNAL ||
        record.diag_codes[0] != SL_DIAG_SQLITE_PROVIDER_ERROR ||
        record.statuses[1] != SL_STATUS_CANCELLED ||
        record.diag_codes[1] != SL_DIAG_ENGINE_CANCELLED || executor.worker_failure_count != 1U ||
        executor.late_completion_count != 1U)
    {
        return 112;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_completion_post_failure_releases_claimed_active_operation(void)
{
    unsigned char arena_storage[16384];
    SlArena arena;
    SlAsyncCompletion completions[1];
    SlAsyncLoop* loop = NULL;
    SlProviderExecutorConfig config = {0};
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[1];
    ProviderRecord record = {0};
    SlAsyncCompletion blocker = {0};
    SlProviderOperation* operation = NULL;
    SlProviderOperation* recovery = NULL;
    SlProviderOperationDescriptor desc;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, completions, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 130;
    }

    config.instance_id = sl_str_from_cstr("sqlite:main");
    config.provider_kind = sl_str_from_cstr("sqlite");
    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.queue_capacity = 1U;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, slots, loop),
                      SL_STATUS_OK) != 0)
    {
        return 131;
    }

    blocker.kind = SL_ASYNC_COMPLETION_TEST;
    blocker.operation_kind = SL_ASYNC_OPERATION_INTERNAL_COMPLETION;
    blocker.dispatch = noop_async_completion;
    if (expect_status(sl_async_loop_post(loop, &blocker), SL_STATUS_OK) != 0 ||
        make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &operation),
                      SL_STATUS_OK) != 0 ||
        operation == NULL)
    {
        return 132;
    }

    operation->worker_claimed = true;
    if (expect_status(sl_provider_operation_complete(operation, sl_status_ok(), SL_DIAG_NONE,
                                                     sl_str_from_cstr("queue saturated")),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        executor.completion_post_failure_count != 1U)
    {
        return 133;
    }
    if (executor.in_flight != 0U) {
        return 135;
    }
    if (executor.count != 0U) {
        return 136;
    }
    if (record.worker_count != 0U) {
        return 137;
    }
    if (record.cleanup_count != 1U) {
        return 138;
    }
    if (record.dispatch_count != 0U) {
        return 139;
    }
    if (sl_provider_operation_state(operation) != SL_PROVIDER_OPERATION_TERMINAL) {
        return 140;
    }

    if (expect_status(sl_async_loop_drain(loop, 1U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &recovery),
                      SL_STATUS_OK) != 0 ||
        recovery == NULL ||
        expect_status(sl_provider_operation_complete(recovery, sl_status_ok(), SL_DIAG_NONE,
                                                     sl_str_from_cstr("recovered")),
                      SL_STATUS_OK) != 0 ||
        drain_until_dispatch_count(loop, &record, 1U) != 0 || record.dispatch_count != 1U ||
        record.cleanup_count != 2U || executor.in_flight != 0U || executor.count != 0U)
    {
        return 134;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_serialized_run_requires_thread_safe_async_backend(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[1];
    ProviderRecord record = {0};
    ProviderWorkPayload payload = {&record, 30, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("ok"),
                                   false};
    SlProviderOperation* operation = NULL;
    SlProviderOperationDescriptor desc;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor(&arena, &loop, &executor, slots, completions, 1U) != 0)
    {
        return 120;
    }

    if (make_worker_descriptor(&record, &payload, &desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &operation),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        operation != NULL || record.worker_count != 0U || record.cleanup_count != 0U)
    {
        return 121;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_per_instance_isolation(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderExecutorConfig config_a = {0};
    SlProviderExecutorConfig config_b = {0};
    SlProviderInstanceExecutor a;
    SlProviderInstanceExecutor b;
    SlProviderExecutorSlot slot_a[1];
    SlProviderExecutorSlot slot_b[1];
    ProviderRecord record = {0};
    SlProviderOperation* op_a = NULL;
    SlProviderOperation* op_b = NULL;
    SlProviderOperation* overflow = NULL;
    SlProviderOperationDescriptor desc_a;
    SlProviderOperationDescriptor desc_b;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, completions, 16U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 40;
    }

    config_a.instance_id = sl_str_from_cstr("sqlite:main");
    config_a.provider_kind = sl_str_from_cstr("sqlite");
    config_a.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config_a.queue_capacity = 1U;
    config_a.worker_count = 1U;
    config_a.max_in_flight = 1U;
    config_b = config_a;
    config_b.instance_id = sl_str_from_cstr("sqlite:audit");
    if (expect_status(sl_provider_executor_init(&a, &arena, &config_a, slot_a, loop),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_init(&b, &arena, &config_b, slot_b, loop),
                      SL_STATUS_OK) != 0)
    {
        return 41;
    }

    if (make_descriptor(&record, sl_bytes_empty(), NULL, &desc_a) != 0) {
        return 42;
    }
    desc_b = desc_a;
    desc_b.provider_instance_id = sl_str_from_cstr("sqlite:audit");
    if (expect_status(sl_provider_executor_submit(&a, &arena, &desc_a, &op_a), SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&a, &arena, &desc_a, &overflow),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_status(sl_provider_executor_submit(&b, &arena, &desc_b, &op_b), SL_STATUS_OK) != 0 ||
        op_a == NULL || op_b == NULL || overflow != NULL ||
        sl_provider_executor_pending_count(&a) != 1U ||
        sl_provider_executor_pending_count(&b) != 1U)
    {
        return 43;
    }

    sl_provider_executor_dispose(&a);
    sl_provider_executor_dispose(&b);
    sl_async_loop_dispose(loop);
    return 0;
}

int main(void)
{
    int result = test_execution_mode_parse_and_validation();

    if (result != 0) {
        return result;
    }

    result = test_serialized_admission_overflow_and_recovery();
    if (result != 0) {
        return result;
    }

    result = test_descriptor_helpers_preserve_outputs_on_failure();
    if (result != 0) {
        return result;
    }

    result = test_invalid_descriptor_fields_fail_without_cleanup();
    if (result != 0) {
        return result;
    }

    result = test_cancel_timeout_and_late_completion_cleanup_once();
    if (result != 0) {
        return result;
    }

    result = test_shutdown_rejects_new_work_and_cancels_pending();
    if (result != 0) {
        return result;
    }

    result = test_blocking_pool_promotes_queued_when_slot_frees();
    if (result != 0) {
        return result;
    }

    result = test_serialized_worker_executes_one_at_a_time_fifo();
    if (result != 0) {
        return result;
    }

    result = test_serialized_worker_capacity_and_reject_ownership();
    if (result != 0) {
        return result;
    }

    result = test_serialized_worker_failure_and_late_completion_cleanup_once();
    if (result != 0) {
        return result;
    }

    result = test_completion_post_failure_releases_claimed_active_operation();
    if (result != 0) {
        return result;
    }

    result = test_serialized_run_requires_thread_safe_async_backend();
    if (result != 0) {
        return result;
    }

    return test_per_instance_isolation();
}
