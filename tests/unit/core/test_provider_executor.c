#include "sloppy/provider_executor.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct ProviderRecord
{
    SlStatusCode statuses[16];
    SlDiagCode diag_codes[16];
    SlAsyncOperationKind operation_kinds[16];
    SlStr messages[16];
    size_t dispatch_count;
    size_t cleanup_count;
} ProviderRecord;

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

static void cleanup_provider_operation(SlProviderOperation* operation, void* user)
{
    ProviderRecord* record = (ProviderRecord*)user;

    (void)operation;
    if (record != NULL) {
        record->cleanup_count += 1U;
    }
}

static SlProviderOperationDescriptor descriptor(ProviderRecord* record, SlBytes input,
                                                SlCancellationToken* token)
{
    SlProviderOperationDescriptor desc = {0};

    desc.provider_instance_id = sl_str_from_cstr("sqlite:main");
    desc.provider_kind = sl_str_from_cstr("sqlite");
    desc.operation_name = sl_str_from_cstr("query");
    desc.capability_token = sl_str_from_cstr("data.main");
    desc.execution_mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    desc.cancellation = token;
    desc.input = input;
    desc.completion_dispatch = record_provider_completion;
    desc.completion_dispatch_user = record;
    desc.cleanup = cleanup_provider_operation;
    desc.cleanup_user = record;
    return desc;
}

static int init_executor(SlArena* arena, SlAsyncLoop** loop, SlProviderInstanceExecutor* executor,
                         SlProviderExecutorSlot* slots, SlAsyncCompletion* completions,
                         size_t capacity)
{
    SlProviderExecutorConfig config = {0};

    config.instance_id = sl_str_from_cstr("sqlite:main");
    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.queue_capacity = capacity;
    config.worker_count = 1U;
    if (expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, arena, completions, 16U, loop),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    return expect_status(sl_provider_executor_init(executor, arena, &config, slots, *loop),
                         SL_STATUS_OK);
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

    first_desc = descriptor(&record, sl_bytes_from_parts(first_bytes, sizeof(first_bytes)), NULL);
    second_desc =
        descriptor(&record, sl_bytes_from_parts(second_bytes, sizeof(second_bytes)), NULL);
    third_desc = descriptor(&record, sl_bytes_from_parts(third_bytes, sizeof(third_bytes)), NULL);
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &first_desc, &first),
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

    cancel_desc = descriptor(&record, sl_bytes_empty(), &token);
    timeout_desc = descriptor(&record, sl_bytes_empty(), NULL);
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &cancel_desc, &cancelled),
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

    desc = descriptor(&record, sl_bytes_empty(), NULL);
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &first),
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
    config_a.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config_a.queue_capacity = 1U;
    config_a.worker_count = 1U;
    config_b = config_a;
    config_b.instance_id = sl_str_from_cstr("sqlite:audit");
    if (expect_status(sl_provider_executor_init(&a, &arena, &config_a, slot_a, loop),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_init(&b, &arena, &config_b, slot_b, loop),
                      SL_STATUS_OK) != 0)
    {
        return 41;
    }

    desc_a = descriptor(&record, sl_bytes_empty(), NULL);
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
        return 42;
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

    result = test_cancel_timeout_and_late_completion_cleanup_once();
    if (result != 0) {
        return result;
    }

    result = test_shutdown_rejects_new_work_and_cancels_pending();
    if (result != 0) {
        return result;
    }

    return test_per_instance_isolation();
}
