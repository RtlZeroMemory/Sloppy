#include "sloppy/provider_executor.h"

#include <stdatomic.h>
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
    atomic_int* active_count;
    atomic_int* max_active_count;
    atomic_int* started_count;
    atomic_bool* release;
    SlPlatformMutex* record_mutex;
    SlStr message;
    int value;
    SlStatusCode status_code;
    SlDiagCode diag_code;
    bool cancel_before_success;
} ProviderWorkPayload;

typedef struct ProviderPolicyHook
{
    SlStr expected_token;
    SlStr expected_provider_token;
    SlStr expected_provider_kind;
    SlCapabilityOperation expected_operation;
    size_t calls;
} ProviderPolicyHook;

typedef int (*ProviderTestFn)(void);

static SlCapabilityRegistry* provider_test_capability_registry(void)
{
    static SlPlanDataProvider providers[3];
    static SlPlanCapability caps[5];
    static SlPlan plan;
    static SlCapabilityRegistry registry;
    static bool initialized = false;

    if (!initialized) {
        providers[0].token = sl_str_from_cstr("data.main");
        providers[0].provider = sl_str_from_cstr("sqlite");
        providers[0].capability = sl_str_empty();
        providers[0].service = sl_str_from_cstr("data.main");
        providers[1].token = sl_str_from_cstr("data.audit");
        providers[1].provider = sl_str_from_cstr("sqlite");
        providers[1].capability = sl_str_empty();
        providers[1].service = sl_str_from_cstr("data.audit");
        providers[2].token = sl_str_from_cstr("data.pg");
        providers[2].provider = sl_str_from_cstr("postgres");
        providers[2].capability = sl_str_empty();
        providers[2].service = sl_str_from_cstr("data.pg");

        caps[0].token = sl_str_from_cstr("data.main");
        caps[0].kind = sl_str_from_cstr("database");
        caps[0].access = sl_str_from_cstr("readwrite");
        caps[0].provider = sl_str_from_cstr("data.main");
        caps[1].token = sl_str_from_cstr("data.audit");
        caps[1].kind = sl_str_from_cstr("database");
        caps[1].access = sl_str_from_cstr("readwrite");
        caps[1].provider = sl_str_from_cstr("data.audit");
        caps[2].token = sl_str_from_cstr("data.pg");
        caps[2].kind = sl_str_from_cstr("database");
        caps[2].access = sl_str_from_cstr("readwrite");
        caps[2].provider = sl_str_from_cstr("data.pg");
        caps[3].token = sl_str_from_cstr("data.readonly");
        caps[3].kind = sl_str_from_cstr("database");
        caps[3].access = sl_str_from_cstr("read");
        caps[3].provider = sl_str_from_cstr("data.main");
        caps[4].token = sl_str_from_cstr("files.assets");
        caps[4].kind = sl_str_from_cstr("filesystem");
        caps[4].access = sl_str_from_cstr("readwrite");
        caps[4].provider = sl_str_empty();

        plan.data_providers = providers;
        plan.data_provider_count = 3U;
        plan.capabilities = caps;
        plan.capability_count = 5U;
        if (!sl_status_is_ok(sl_capability_registry_init_from_plan(&plan, &registry))) {
            return NULL;
        }
        initialized = true;
    }

    return &registry;
}

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static bool provider_diag_has_hint(const SlDiag* diag, const char* expected)
{
    size_t index = 0U;
    SlStr hint = sl_str_from_cstr(expected);

    if (diag == NULL) {
        return false;
    }
    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (sl_str_equal(diag->hints[index], hint)) {
            return true;
        }
    }
    return false;
}

static SlStatus provider_test_capability_check(const SlCapabilityRegistry* registry,
                                               SlArena* diag_arena, SlStr token,
                                               SlCapabilityOperation operation,
                                               SlStr provider_token, SlStr provider_kind,
                                               SlDiag* out_diag, void* user)
{
    ProviderPolicyHook* hook = (ProviderPolicyHook*)user;

    (void)registry;
    (void)diag_arena;
    (void)out_diag;

    if (hook == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    hook->calls += 1U;
    if (!sl_str_equal(token, hook->expected_token) ||
        !sl_str_equal(provider_token, hook->expected_provider_token) ||
        !sl_str_equal(provider_kind, hook->expected_provider_kind) ||
        operation != hook->expected_operation)
    {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_status_ok();
}

static SlStatus provider_test_database_capability_check(const SlCapabilityRegistry* registry,
                                                        SlArena* diag_arena, SlStr token,
                                                        SlCapabilityOperation operation,
                                                        SlStr provider_token, SlStr provider_kind,
                                                        SlDiag* out_diag, void* user)
{
    SlStatus status;

    (void)user;

    status = sl_capability_check_database(registry, diag_arena, token, operation, provider_token,
                                          out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_capability_check_database_provider(registry, diag_arena, token, operation,
                                                 provider_kind, out_diag);
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

    if (payload->record_mutex != NULL) {
        sl_platform_mutex_lock(payload->record_mutex);
    }
    index = record->worker_count;
    record->worker_values[index] = payload->value;
    record->worker_count += 1U;
    if (payload->record_mutex != NULL) {
        sl_platform_mutex_unlock(payload->record_mutex);
    }
    if (payload->active_count != NULL) {
        int active = atomic_fetch_add(payload->active_count, 1) + 1;
        if (payload->max_active_count != NULL) {
            int observed = atomic_load(payload->max_active_count);
            while (active > observed &&
                   !atomic_compare_exchange_weak(payload->max_active_count, &observed, active))
            {
            }
        }
    }
    if (payload->started_count != NULL) {
        atomic_fetch_add(payload->started_count, 1);
    }
    while (payload->release != NULL && !atomic_load(payload->release)) {
    }
    if (payload->cancel_before_success) {
        (void)sl_provider_operation_cancel(operation, sl_str_from_cstr("worker cancelled"));
    }
    if (out_diag_code != NULL) {
        *out_diag_code = payload->diag_code;
    }
    if (out_message != NULL) {
        *out_message = payload->message;
    }
    if (payload->active_count != NULL) {
        atomic_fetch_sub(payload->active_count, 1);
    }
    return sl_status_from_code(payload->status_code);
}

static ProviderWorkPayload provider_payload_basic(ProviderRecord* record, int value,
                                                  SlStatusCode status_code, SlDiagCode diag_code,
                                                  SlStr message, bool cancel_before_success)
{
    ProviderWorkPayload payload = {0};

    payload.record = record;
    payload.value = value;
    payload.status_code = status_code;
    payload.diag_code = diag_code;
    payload.message = message;
    payload.cancel_before_success = cancel_before_success;
    return payload;
}

static int make_descriptor_for(ProviderRecord* record, SlBytes input, SlCancellationToken* token,
                               SlStr instance_id, SlStr provider_kind, SlProviderExecutionMode mode,
                               SlProviderOperationDescriptor* out_descriptor)
{
    SlStr capability_token = sl_str_from_cstr("data.main");
    SlProviderOperationDescriptor desc = sl_provider_operation_descriptor_init(
        instance_id, provider_kind, SL_PROVIDER_OPERATION_KIND_QUERY, sl_str_from_cstr("query"),
        mode, record_provider_completion, record);

    if (out_descriptor == NULL) {
        return 1;
    }
    if (sl_str_equal(provider_kind, sl_str_from_cstr("postgres"))) {
        capability_token = sl_str_from_cstr("data.pg");
    }
    else if (sl_str_equal(instance_id, sl_str_from_cstr("sqlite:audit"))) {
        capability_token = sl_str_from_cstr("data.audit");
    }
    if (expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc, capability_token, SL_CAPABILITY_OPERATION_READ),
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

static int make_worker_descriptor_for(ProviderRecord* record, ProviderWorkPayload* payload,
                                      SlStr instance_id, SlStr provider_kind,
                                      SlProviderExecutionMode mode,
                                      SlProviderOperationDescriptor* out_descriptor)
{
    if (make_descriptor_for(record, sl_bytes_empty(), NULL, instance_id, provider_kind, mode,
                            out_descriptor) != 0)
    {
        return 1;
    }
    return expect_status(sl_provider_operation_descriptor_attach_run(
                             out_descriptor, run_provider_like_operation, payload),
                         SL_STATUS_OK);
}

static int wait_until_atomic_at_least(atomic_int* value, int expected)
{
    size_t attempts = 0U;

    if (value == NULL) {
        return 1;
    }

    for (attempts = 0U; attempts < 10000000U; attempts += 1U) {
        if (atomic_load(value) >= expected) {
            return 0;
        }
    }

    return 2;
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

static SlProviderExecutorConfig provider_sqlite_serialized_config(size_t capacity)
{
    SlProviderExecutorConfig config = {0};

    config.instance_id = sl_str_from_cstr("sqlite:main");
    config.provider_kind = sl_str_from_cstr("sqlite");
    config.provider_token = sl_str_from_cstr("data.main");
    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.queue_capacity = capacity;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    config.capability_registry = provider_test_capability_registry();
    config.capability_check = provider_test_database_capability_check;
    return config;
}

static int init_executor_with_backend(SlArena* arena, SlAsyncLoop** loop,
                                      SlProviderInstanceExecutor* executor,
                                      SlProviderExecutorSlot* slots, SlAsyncCompletion* completions,
                                      size_t capacity, SlAsyncBackendKind backend)
{
    SlProviderExecutorConfig config = {0};

    config = provider_sqlite_serialized_config(capacity);
    if (expect_status(sl_async_loop_create(backend, arena, completions, 16U, loop), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }
    if (expect_status(sl_provider_executor_init(executor, arena, &config, slots, *loop),
                      SL_STATUS_OK) != 0)
    {
        sl_async_loop_dispose(*loop);
        *loop = NULL;
        return 2;
    }
    return 0;
}

static int init_executor(SlArena* arena, SlAsyncLoop** loop, SlProviderInstanceExecutor* executor,
                         SlProviderExecutorSlot* slots, SlAsyncCompletion* completions,
                         size_t capacity)
{
    return init_executor_with_backend(arena, loop, executor, slots, completions, capacity,
                                      SL_ASYNC_BACKEND_TEST);
}

static int init_blocking_pool_executor(SlArena* arena, SlAsyncLoop** loop,
                                       SlProviderInstanceExecutor* executor,
                                       SlProviderExecutorSlot* slots,
                                       SlAsyncCompletion* completions, size_t capacity,
                                       size_t worker_count, size_t max_in_flight)
{
    SlProviderExecutorConfig config = {0};

    config.instance_id = sl_str_from_cstr("postgres:main");
    config.provider_kind = sl_str_from_cstr("postgres");
    config.provider_token = sl_str_from_cstr("data.pg");
    config.mode = SL_PROVIDER_EXECUTION_BLOCKING_POOL;
    config.queue_capacity = capacity;
    config.worker_count = worker_count;
    config.max_in_flight = max_in_flight;
    config.capability_registry = provider_test_capability_registry();
    config.capability_check = provider_test_database_capability_check;
    if (expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, arena, completions, 16U, loop),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_status(sl_provider_executor_init(executor, arena, &config, slots, *loop),
                      SL_STATUS_OK) != 0)
    {
        sl_async_loop_dispose(*loop);
        *loop = NULL;
        return 2;
    }
    return 0;
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
    SlProviderInstanceExecutor executor = {0};
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

static int test_pre_cancelled_and_expired_deadline_reject_before_enqueue(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor = {0};
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    SlCancellationToken cancelled;
    SlCancellationToken expired;
    SlProviderOperation* op = (SlProviderOperation*)1;
    SlProviderOperationDescriptor desc;
    SlDiag diag = {0};

    sl_cancellation_token_init(&cancelled);
    sl_cancellation_token_init(&expired);
    if (expect_status(sl_cancellation_token_cancel(&cancelled, SL_CANCELLATION_REASON_CANCELLED,
                                                   sl_str_from_cstr("request cancelled")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_cancellation_token_cancel(&expired,
                                                   SL_CANCELLATION_REASON_DEADLINE_EXCEEDED,
                                                   sl_str_from_cstr("deadline expired")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor(&arena, &loop, &executor, slots, completions, 2U) != 0)
    {
        return 200;
    }

    if (make_descriptor(&record, sl_bytes_empty(), &cancelled, &desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_CANCELLED) != 0 ||
        op != NULL || sl_provider_executor_pending_count(&executor) != 0U ||
        record.cleanup_count != 0U)
    {
        return 201;
    }

    op = (SlProviderOperation*)1;
    if (make_descriptor(&record, sl_bytes_empty(), &expired, &desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        op != NULL || sl_provider_executor_pending_count(&executor) != 0U ||
        record.cleanup_count != 0U)
    {
        return 202;
    }

    op = (SlProviderOperation*)1;
    diag = (SlDiag){0};
    if (make_descriptor(&record, sl_bytes_empty(), &cancelled, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 203;
    }
    desc.capability.token = sl_str_empty();
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_CANCELLED) != 0 ||
        op != NULL || diag.code == SL_DIAG_PERMISSION_DENIED ||
        sl_provider_executor_pending_count(&executor) != 0U)
    {
        return 204;
    }

    op = (SlProviderOperation*)1;
    diag = (SlDiag){0};
    if (make_descriptor(&record, sl_bytes_empty(), &expired, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 205;
    }
    desc.capability.token = sl_str_empty();
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        op != NULL || diag.code == SL_DIAG_PERMISSION_DENIED ||
        sl_provider_executor_pending_count(&executor) != 0U)
    {
        return 206;
    }

    op = (SlProviderOperation*)1;
    diag = (SlDiag){0};
    if (expect_status(sl_provider_executor_shutdown(&executor,
                                                    SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL),
                      SL_STATUS_OK) != 0 ||
        make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 207;
    }
    desc.capability.token = sl_str_empty();
    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_CANCELLED) != 0 ||
        op != NULL || diag.code == SL_DIAG_PERMISSION_DENIED ||
        executor.shutdown_rejected_count != 1U)
    {
        return 208;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_capability_denials_reject_before_enqueue(void)
{
    unsigned char arena_storage[16384];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor = {0};
    SlProviderExecutorSlot slots[1];
    ProviderRecord record = {0};
    ProviderWorkPayload payload = provider_payload_basic(&record, 42, SL_STATUS_OK, SL_DIAG_NONE,
                                                         sl_str_from_cstr("ok"), false);
    SlProviderOperation* op = (SlProviderOperation*)1;
    SlProviderOperationDescriptor desc;
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor_with_backend(&arena, &loop, &executor, slots, completions, 1U,
                                   SL_ASYNC_BACKEND_LIBUV) != 0)
    {
        return 210;
    }

    if (make_worker_descriptor(&record, &payload, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc, sl_str_from_cstr("data.missing"), SL_CAPABILITY_OPERATION_READ),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_STATE) != 0 ||
        op != NULL || record.worker_count != 0U || record.cleanup_count != 0U ||
        sl_provider_executor_pending_count(&executor) != 0U ||
        diag.code != SL_DIAG_PERMISSION_DENIED ||
        !provider_diag_has_hint(&diag, "token: data.missing"))
    {
        return 211;
    }

    op = (SlProviderOperation*)1;
    diag = (SlDiag){0};
    if (make_worker_descriptor(&record, &payload, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc, sl_str_from_cstr("data.readonly"), SL_CAPABILITY_OPERATION_WRITE),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_STATE) != 0 ||
        op != NULL || record.worker_count != 0U || record.cleanup_count != 0U ||
        diag.code != SL_DIAG_PERMISSION_DENIED ||
        !provider_diag_has_hint(&diag, "actual access: read"))
    {
        return 212;
    }

    op = (SlProviderOperation*)1;
    diag = (SlDiag){0};
    if (make_worker_descriptor(&record, &payload, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc, sl_str_from_cstr("data.audit"), SL_CAPABILITY_OPERATION_READ),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_STATE) != 0 ||
        op != NULL || record.worker_count != 0U || record.cleanup_count != 0U ||
        diag.code != SL_DIAG_PERMISSION_DENIED ||
        !provider_diag_has_hint(&diag, "provider: data.main"))
    {
        return 213;
    }

    op = (SlProviderOperation*)1;
    diag = (SlDiag){0};
    if (make_worker_descriptor(&record, &payload, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc, sl_str_from_cstr("files.assets"), SL_CAPABILITY_OPERATION_READ),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_STATE) != 0 ||
        op != NULL || record.worker_count != 0U || record.cleanup_count != 0U ||
        diag.code != SL_DIAG_PERMISSION_DENIED ||
        !provider_diag_has_hint(&diag, "kind: filesystem"))
    {
        return 214;
    }

    op = (SlProviderOperation*)1;
    diag = (SlDiag){0};
    if (make_worker_descriptor(&record, &payload, &desc) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_provider_operation_descriptor_attach_capability(
                &desc, sl_str_from_cstr("data.readonly"), SL_CAPABILITY_OPERATION_READWRITE),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_STATE) != 0 ||
        op != NULL || record.worker_count != 0U || record.cleanup_count != 0U ||
        diag.code != SL_DIAG_PERMISSION_DENIED ||
        !provider_diag_has_hint(&diag, "operation: readwrite"))
    {
        return 215;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_missing_capability_redacts_unsafe_admission_hints(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[4];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor = {0};
    SlProviderExecutorSlot slots[1];
    SlProviderExecutorConfig config = {0};
    ProviderRecord record = {0};
    ProviderPolicyHook hook = {0};
    SlProviderOperationDescriptor desc;
    SlProviderOperation* op = (SlProviderOperation*)1;
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, completions, 4U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 216;
    }

    config.instance_id = sl_str_from_cstr("addon:main");
    config.provider_kind = sl_str_from_cstr("native-addon");
    config.provider_token = sl_str_from_cstr("postgres://user:secret@example/db");
    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.queue_capacity = 1U;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    config.capability_check = provider_test_capability_check;
    config.capability_check_user = &hook;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, slots, loop),
                      SL_STATUS_OK) != 0)
    {
        sl_async_loop_dispose(loop);
        return 217;
    }

    desc = sl_provider_operation_descriptor_init(
        config.instance_id, config.provider_kind, SL_PROVIDER_OPERATION_KIND_INTERNAL,
        sl_str_from_cstr("load secret"), config.mode, record_provider_completion, &record);
    if (expect_status(sl_provider_operation_descriptor_attach_admission_diag(&desc, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op),
                      SL_STATUS_INVALID_STATE) != 0 ||
        op != NULL || diag.code != SL_DIAG_PERMISSION_DENIED ||
        !provider_diag_has_hint(&diag, "operation: <redacted>") ||
        !provider_diag_has_hint(&diag, "provider: <redacted>"))
    {
        sl_provider_executor_dispose(&executor);
        sl_async_loop_dispose(loop);
        return 218;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_custom_capability_hook_allows_non_database_provider(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor = {0};
    SlProviderExecutorSlot slots[2];
    SlProviderExecutorConfig config = {0};
    ProviderRecord record = {0};
    ProviderPolicyHook hook = {0};
    ProviderWorkPayload payload;
    SlProviderOperationDescriptor desc;
    SlProviderOperation* op = NULL;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 161;
    }
    if (expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, completions, 16U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 162;
    }

    hook.expected_token = sl_str_from_cstr("addons.image.resize");
    hook.expected_provider_token = sl_str_from_cstr("addons.image");
    hook.expected_provider_kind = sl_str_from_cstr("image-codec");
    hook.expected_operation = SL_CAPABILITY_OPERATION_READWRITE;

    config.instance_id = sl_str_from_cstr("image-codec:main");
    config.provider_kind = hook.expected_provider_kind;
    config.provider_token = hook.expected_provider_token;
    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.queue_capacity = 2U;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    config.capability_check = provider_test_capability_check;
    config.capability_check_user = &hook;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, slots, loop),
                      SL_STATUS_OK) != 0)
    {
        sl_async_loop_dispose(loop);
        return 163;
    }

    payload = provider_payload_basic(&record, 55, SL_STATUS_OK, SL_DIAG_NONE,
                                     sl_str_from_cstr("image provider completed"), false);
    desc = sl_provider_operation_descriptor_init(
        config.instance_id, config.provider_kind, SL_PROVIDER_OPERATION_KIND_INTERNAL,
        sl_str_from_cstr("resize"), config.mode, record_provider_completion, &record);
    if (expect_status(sl_provider_operation_descriptor_attach_capability(&desc, hook.expected_token,
                                                                         hook.expected_operation),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_cleanup(
                          &desc, cleanup_provider_operation, &record),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_operation_descriptor_attach_run(
                          &desc, run_provider_like_operation, &payload),
                      SL_STATUS_OK) != 0)
    {
        sl_provider_executor_dispose(&executor);
        sl_async_loop_dispose(loop);
        return 164;
    }

    if (expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &op), SL_STATUS_OK) !=
            0 ||
        op == NULL || hook.calls != 1U)
    {
        sl_provider_executor_dispose(&executor);
        sl_async_loop_dispose(loop);
        return 165;
    }

    if (drain_until_dispatch_count(loop, &record, 1U) != 0 || record.worker_count != 1U ||
        record.worker_values[0] != 55 || record.cleanup_count != 1U)
    {
        sl_provider_executor_dispose(&executor);
        sl_async_loop_dispose(loop);
        return 166;
    }

    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_blocking_pool_invalid_config_rejected(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[1];
    SlAsyncLoop* loop = NULL;
    SlProviderExecutorConfig config = {0};
    SlProviderInstanceExecutor executor = {0};
    SlProviderExecutorSlot slots[1];

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, completions, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 150;
    }

    config.instance_id = sl_str_from_cstr("postgres:main");
    config.provider_kind = sl_str_from_cstr("postgres");
    config.provider_token = sl_str_from_cstr("data.pg");
    config.mode = SL_PROVIDER_EXECUTION_BLOCKING_POOL;
    config.queue_capacity = 1U;
    config.worker_count = 0U;
    config.max_in_flight = 0U;
    config.capability_registry = provider_test_capability_registry();
    config.capability_check = provider_test_database_capability_check;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, slots, loop),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        sl_async_loop_dispose(loop);
        return 151;
    }

    config.worker_count = 1U;
    config.max_in_flight = 2U;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, slots, loop),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        sl_async_loop_dispose(loop);
        return 152;
    }

    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.worker_count = 1U;
    config.max_in_flight = 2U;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, slots, loop),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        sl_async_loop_dispose(loop);
        return 153;
    }

    sl_async_loop_dispose(loop);
    return 0;
}

static int test_serialized_admission_overflow_and_recovery(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor = {0};
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

static int test_queued_cancel_prevents_worker_execution(void)
{
    unsigned char arena_storage[16384];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    atomic_int started_count;
    atomic_bool release;
    ProviderWorkPayload active_payload;
    ProviderWorkPayload queued_payload;
    SlProviderOperation* active = NULL;
    SlProviderOperation* queued = NULL;
    SlProviderOperationDescriptor active_desc;
    SlProviderOperationDescriptor queued_desc;
    int result = 0;

    atomic_init(&started_count, 0);
    atomic_init(&release, false);
    active_payload = provider_payload_basic(&record, 80, SL_STATUS_OK, SL_DIAG_NONE,
                                            sl_str_from_cstr("active done"), false);
    active_payload.started_count = &started_count;
    active_payload.release = &release;
    queued_payload = provider_payload_basic(&record, 81, SL_STATUS_OK, SL_DIAG_NONE,
                                            sl_str_from_cstr("queued done"), false);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor_with_backend(&arena, &loop, &executor, slots, completions, 2U,
                                   SL_ASYNC_BACKEND_LIBUV) != 0 ||
        make_worker_descriptor(&record, &active_payload, &active_desc) != 0 ||
        make_worker_descriptor(&record, &queued_payload, &queued_desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &active_desc, &active),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &queued_desc, &queued),
                      SL_STATUS_OK) != 0 ||
        wait_until_atomic_at_least(&started_count, 1) != 0)
    {
        result = 220;
        goto cleanup;
    }

    if (expect_status(sl_provider_operation_cancel(queued, sl_str_from_cstr("queued cancelled")),
                      SL_STATUS_OK) != 0 ||
        drain_until_dispatch_count(loop, &record, 1U) != 0 ||
        record.statuses[0] != SL_STATUS_CANCELLED ||
        record.diag_codes[0] != SL_DIAG_ENGINE_CANCELLED || record.cleanup_count != 1U ||
        sl_provider_operation_state(queued) != SL_PROVIDER_OPERATION_TERMINAL)
    {
        result = 221;
        goto cleanup;
    }

    atomic_store(&release, true);
    if (drain_until_dispatch_count(loop, &record, 2U) != 0 || record.worker_count != 1U ||
        record.worker_values[0] != 80 || record.dispatch_count != 2U ||
        record.cleanup_count != 2U || sl_provider_executor_pending_count(&executor) != 0U ||
        sl_provider_executor_in_flight_count(&executor) != 0U)
    {
        result = 222;
        goto cleanup;
    }

cleanup:
    atomic_store(&release, true);
    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    return result;
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
    config.provider_token = sl_str_from_cstr("data.pg");
    config.mode = SL_PROVIDER_EXECUTION_BLOCKING_POOL;
    config.queue_capacity = 2U;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    config.capability_registry = provider_test_capability_registry();
    config.capability_check = provider_test_database_capability_check;
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

static int test_blocking_pool_workers_cap_parallel_execution_and_fifo_queue(void)
{
    unsigned char arena_storage[32768];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[4];
    ProviderRecord record = {0};
    SlPlatformMutex* record_mutex = NULL;
    atomic_int active_count;
    atomic_int max_active_count;
    atomic_int started_count;
    atomic_bool release;
    ProviderWorkPayload payloads[4];
    SlProviderOperation* operations[4] = {NULL, NULL, NULL, NULL};
    SlProviderOperationDescriptor descriptors[4];
    size_t index = 0U;

    atomic_init(&active_count, 0);
    atomic_init(&max_active_count, 0);
    atomic_init(&started_count, 0);
    atomic_init(&release, false);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_platform_mutex_create(&arena, &record_mutex), SL_STATUS_OK) != 0 ||
        init_blocking_pool_executor(&arena, &loop, &executor, slots, completions, 4U, 2U, 2U) != 0)
    {
        return 160;
    }

    for (index = 0U; index < 4U; index += 1U) {
        payloads[index] = provider_payload_basic(&record, (int)(index + 1U), SL_STATUS_OK,
                                                 SL_DIAG_NONE, sl_str_from_cstr("ok"), false);
        payloads[index].active_count = &active_count;
        payloads[index].max_active_count = &max_active_count;
        payloads[index].started_count = &started_count;
        payloads[index].release = &release;
        payloads[index].record_mutex = record_mutex;
        if (make_worker_descriptor_for(&record, &payloads[index], sl_str_from_cstr("postgres:main"),
                                       sl_str_from_cstr("postgres"),
                                       SL_PROVIDER_EXECUTION_BLOCKING_POOL,
                                       &descriptors[index]) != 0 ||
            expect_status(sl_provider_executor_submit(&executor, &arena, &descriptors[index],
                                                      &operations[index]),
                          SL_STATUS_OK) != 0)
        {
            return 161;
        }
    }

    if (wait_until_atomic_at_least(&started_count, 2) != 0 ||
        sl_provider_executor_in_flight_count(&executor) != 2U ||
        sl_provider_executor_pending_count(&executor) != 4U ||
        sl_provider_operation_state(operations[0]) != SL_PROVIDER_OPERATION_ACTIVE ||
        sl_provider_operation_state(operations[1]) != SL_PROVIDER_OPERATION_ACTIVE ||
        sl_provider_operation_state(operations[2]) != SL_PROVIDER_OPERATION_QUEUED ||
        sl_provider_operation_state(operations[3]) != SL_PROVIDER_OPERATION_QUEUED ||
        executor.worker_started_count != 2U)
    {
        return 162;
    }

    atomic_store(&release, true);
    if (drain_until_dispatch_count(loop, &record, 4U) != 0 || record.dispatch_count != 4U ||
        record.cleanup_count != 4U || atomic_load(&max_active_count) != 2 ||
        sl_provider_executor_in_flight_count(&executor) != 0U ||
        sl_provider_executor_pending_count(&executor) != 0U)
    {
        return 163;
    }

    sl_provider_executor_dispose(&executor);
    if (executor.worker_stopped_count != 2U) {
        return 164;
    }
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_blocking_pool_overflow_shutdown_and_cleanup_once(void)
{
    unsigned char arena_storage[32768];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[3];
    ProviderRecord record = {0};
    SlPlatformMutex* record_mutex = NULL;
    atomic_int active_count;
    atomic_int max_active_count;
    atomic_int started_count;
    atomic_bool release;
    ProviderWorkPayload payloads[4];
    SlProviderOperation* first = NULL;
    SlProviderOperation* second = NULL;
    SlProviderOperation* third = NULL;
    SlProviderOperation* overflow = NULL;
    SlProviderOperation* rejected = NULL;
    SlProviderOperationDescriptor descs[4];

    atomic_init(&active_count, 0);
    atomic_init(&max_active_count, 0);
    atomic_init(&started_count, 0);
    atomic_init(&release, false);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_platform_mutex_create(&arena, &record_mutex), SL_STATUS_OK) != 0 ||
        init_blocking_pool_executor(&arena, &loop, &executor, slots, completions, 3U, 2U, 2U) != 0)
    {
        return 170;
    }

    for (size_t index = 0U; index < 4U; index += 1U) {
        payloads[index] = provider_payload_basic(
            &record, (int)(index + 10U), index == 0U ? SL_STATUS_INTERNAL : SL_STATUS_OK,
            index == 0U ? SL_DIAG_POSTGRES_PROVIDER_ERROR : SL_DIAG_NONE,
            index == 0U ? sl_str_from_cstr("provider failed") : sl_str_from_cstr("ok"), false);
        payloads[index].active_count = &active_count;
        payloads[index].max_active_count = &max_active_count;
        payloads[index].started_count = &started_count;
        payloads[index].release = &release;
        payloads[index].record_mutex = record_mutex;
        if (make_worker_descriptor_for(&record, &payloads[index], sl_str_from_cstr("postgres:main"),
                                       sl_str_from_cstr("postgres"),
                                       SL_PROVIDER_EXECUTION_BLOCKING_POOL, &descs[index]) != 0)
        {
            return 171;
        }
    }

    if (expect_status(sl_provider_executor_submit(&executor, &arena, &descs[0], &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &descs[1], &second),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &descs[2], &third),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &descs[3], &overflow),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        overflow != NULL || executor.overflow_count != 1U || record.cleanup_count != 0U)
    {
        return 172;
    }

    if (wait_until_atomic_at_least(&started_count, 2) != 0 ||
        sl_provider_operation_state(third) != SL_PROVIDER_OPERATION_QUEUED)
    {
        return 173;
    }

    if (expect_status(sl_provider_executor_shutdown(&executor,
                                                    SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &descs[3], &rejected),
                      SL_STATUS_CANCELLED) != 0 ||
        rejected != NULL || !sl_provider_executor_is_shutting_down(&executor))
    {
        return 174;
    }

    if (drain_until_dispatch_count(loop, &record, 3U) != 0 || record.cleanup_count != 1U ||
        record.statuses[0] != SL_STATUS_CANCELLED ||
        record.diag_codes[0] != SL_DIAG_APP_LIFECYCLE ||
        record.statuses[1] != SL_STATUS_CANCELLED ||
        record.diag_codes[1] != SL_DIAG_APP_LIFECYCLE ||
        record.statuses[2] != SL_STATUS_CANCELLED || record.diag_codes[2] != SL_DIAG_APP_LIFECYCLE)
    {
        return 175;
    }

    atomic_store(&release, true);
    sl_provider_executor_dispose(&executor);
    if (record.dispatch_count != 3U || record.cleanup_count != 3U ||
        executor.worker_failure_count != 1U || executor.late_completion_count != 2U ||
        sl_provider_executor_pending_count(&executor) != 0U ||
        sl_provider_executor_in_flight_count(&executor) != 0U)
    {
        return 176;
    }

    sl_async_loop_dispose(loop);
    (void)first;
    (void)second;
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
    ProviderWorkPayload first_payload = provider_payload_basic(
        &record, 1, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("one"), false);
    ProviderWorkPayload second_payload = provider_payload_basic(
        &record, 2, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("two"), false);
    ProviderWorkPayload third_payload = provider_payload_basic(
        &record, 3, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("three"), false);
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
    ProviderWorkPayload first_payload = provider_payload_basic(
        &record, 10, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("ten"), false);
    ProviderWorkPayload rejected_payload = provider_payload_basic(
        &record, 11, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("eleven"), false);
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
    ProviderWorkPayload failure_payload =
        provider_payload_basic(&record, 20, SL_STATUS_INTERNAL, SL_DIAG_SQLITE_PROVIDER_ERROR,
                               sl_str_from_cstr("provider failure"), false);
    ProviderWorkPayload late_payload = provider_payload_basic(
        &record, 21, SL_STATUS_OK, SL_DIAG_NONE, sl_str_from_cstr("late success"), true);
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

    config = provider_sqlite_serialized_config(1U);
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

static int test_shutdown_leaves_worker_claimed_operation_for_worker_completion(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor;
    SlProviderExecutorSlot slots[1];
    ProviderRecord record = {0};
    ProviderWorkPayload payload = provider_payload_basic(&record, 60, SL_STATUS_OK, SL_DIAG_NONE,
                                                         sl_str_from_cstr("ok"), false);
    SlProviderOperation* operation = NULL;
    SlProviderOperationDescriptor desc;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor(&arena, &loop, &executor, slots, completions, 1U) != 0 ||
        make_descriptor(&record, sl_bytes_empty(), NULL, &desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &desc, &operation),
                      SL_STATUS_OK) != 0 ||
        operation == NULL)
    {
        return 141;
    }

    operation->run = run_provider_like_operation;
    operation->run_user = &payload;
    operation->worker_claimed = true;
    if (expect_status(sl_provider_executor_shutdown(&executor,
                                                    SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL),
                      SL_STATUS_OK) != 0 ||
        sl_provider_operation_state(operation) != SL_PROVIDER_OPERATION_TERMINAL ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.dispatch_count != 1U || record.cleanup_count != 0U ||
        record.statuses[0] != SL_STATUS_CANCELLED ||
        record.diag_codes[0] != SL_DIAG_APP_LIFECYCLE ||
        sl_provider_executor_pending_count(&executor) != 1U ||
        sl_provider_executor_in_flight_count(&executor) != 1U ||
        !sl_provider_executor_is_shutting_down(&executor))
    {
        return 142;
    }

    if (expect_status(sl_provider_operation_complete(operation, sl_status_ok(), SL_DIAG_NONE,
                                                     sl_str_from_cstr("late shutdown")),
                      SL_STATUS_INVALID_STATE) != 0 ||
        executor.late_completion_count != 0U || record.cleanup_count != 0U ||
        sl_provider_executor_pending_count(&executor) != 1U ||
        sl_provider_executor_in_flight_count(&executor) != 1U || !operation->worker_claimed)
    {
        return 143;
    }

    sl_provider_executor_dispose(&executor);
    if (record.cleanup_count != 1U) {
        return 144;
    }
    sl_async_loop_dispose(loop);
    return 0;
}

static int test_shutdown_of_queued_operation_preserves_active_in_flight(void)
{
    unsigned char arena_storage[16384];
    SlArena arena;
    SlAsyncCompletion completions[16];
    SlAsyncLoop* loop = NULL;
    SlProviderInstanceExecutor executor = {0};
    SlProviderExecutorSlot slots[2];
    ProviderRecord record = {0};
    atomic_int started_count;
    atomic_bool release;
    ProviderWorkPayload active_payload;
    ProviderWorkPayload queued_payload;
    SlProviderOperation* active = NULL;
    SlProviderOperation* queued = NULL;
    SlProviderOperationDescriptor active_desc;
    SlProviderOperationDescriptor queued_desc;
    int result = 0;

    atomic_init(&started_count, 0);
    atomic_init(&release, false);
    active_payload = provider_payload_basic(&record, 70, SL_STATUS_OK, SL_DIAG_NONE,
                                            sl_str_from_cstr("active done"), false);
    active_payload.started_count = &started_count;
    active_payload.release = &release;
    queued_payload = provider_payload_basic(&record, 71, SL_STATUS_OK, SL_DIAG_NONE,
                                            sl_str_from_cstr("queued done"), false);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        init_executor_with_backend(&arena, &loop, &executor, slots, completions, 2U,
                                   SL_ASYNC_BACKEND_LIBUV) != 0 ||
        make_worker_descriptor(&record, &active_payload, &active_desc) != 0 ||
        make_worker_descriptor(&record, &queued_payload, &queued_desc) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &active_desc, &active),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_provider_executor_submit(&executor, &arena, &queued_desc, &queued),
                      SL_STATUS_OK) != 0 ||
        wait_until_atomic_at_least(&started_count, 1) != 0)
    {
        result = 180;
        goto cleanup;
    }

    if (sl_provider_executor_in_flight_count(&executor) != 1U ||
        sl_provider_operation_state(queued) != SL_PROVIDER_OPERATION_QUEUED)
    {
        result = 181;
        goto cleanup;
    }

    if (expect_status(sl_provider_executor_shutdown(&executor,
                                                    SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL),
                      SL_STATUS_OK) != 0 ||
        drain_until_dispatch_count(loop, &record, 2U) != 0 ||
        record.statuses[0] != SL_STATUS_CANCELLED ||
        record.diag_codes[0] != SL_DIAG_APP_LIFECYCLE ||
        record.statuses[1] != SL_STATUS_CANCELLED ||
        record.diag_codes[1] != SL_DIAG_APP_LIFECYCLE || record.cleanup_count != 1U ||
        sl_provider_executor_in_flight_count(&executor) != 1U ||
        sl_provider_executor_pending_count(&executor) != 1U)
    {
        result = 182;
        goto cleanup;
    }

    atomic_store(&release, true);
    if (drain_until_dispatch_count(loop, &record, 2U) != 0 ||
        sl_provider_executor_in_flight_count(&executor) != 0U ||
        sl_provider_executor_pending_count(&executor) != 0U || record.cleanup_count != 2U)
    {
        result = 183;
        goto cleanup;
    }

cleanup:
    atomic_store(&release, true);
    sl_provider_executor_dispose(&executor);
    sl_async_loop_dispose(loop);
    (void)active;
    return result;
}

static int test_serialized_dispose_stops_zero_capacity_worker(void)
{
    unsigned char arena_storage[8192];
    SlArena arena;
    SlAsyncCompletion completions[1];
    SlAsyncLoop* loop = NULL;
    SlProviderExecutorConfig config = {0};
    SlProviderInstanceExecutor executor;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &arena, completions, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 144;
    }

    config.instance_id = sl_str_from_cstr("sqlite:empty");
    config.provider_kind = sl_str_from_cstr("sqlite");
    config.provider_token = sl_str_from_cstr("data.main");
    config.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config.queue_capacity = 0U;
    config.worker_count = 1U;
    config.max_in_flight = 1U;
    config.capability_registry = provider_test_capability_registry();
    config.capability_check = provider_test_database_capability_check;
    if (expect_status(sl_provider_executor_init(&executor, &arena, &config, NULL, loop),
                      SL_STATUS_OK) != 0)
    {
        sl_async_loop_dispose(loop);
        return 145;
    }

    sl_provider_executor_dispose(&executor);
    if (executor.worker_started_count != 1U || executor.worker_stopped_count != 1U) {
        return 146;
    }

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
    ProviderWorkPayload payload = provider_payload_basic(&record, 30, SL_STATUS_OK, SL_DIAG_NONE,
                                                         sl_str_from_cstr("ok"), false);
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
    config_a.provider_token = sl_str_from_cstr("data.main");
    config_a.mode = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
    config_a.queue_capacity = 1U;
    config_a.worker_count = 1U;
    config_a.max_in_flight = 1U;
    config_a.capability_registry = provider_test_capability_registry();
    config_a.capability_check = provider_test_database_capability_check;
    config_b = config_a;
    config_b.instance_id = sl_str_from_cstr("sqlite:audit");
    config_b.provider_token = sl_str_from_cstr("data.audit");
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
    if (expect_status(sl_provider_operation_descriptor_attach_capability(
                          &desc_b, sl_str_from_cstr("data.audit"), SL_CAPABILITY_OPERATION_READ),
                      SL_STATUS_OK) != 0)
    {
        return 42;
    }
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
    ProviderTestFn tests[] = {test_execution_mode_parse_and_validation,
                              test_serialized_admission_overflow_and_recovery,
                              test_descriptor_helpers_preserve_outputs_on_failure,
                              test_invalid_descriptor_fields_fail_without_cleanup,
                              test_pre_cancelled_and_expired_deadline_reject_before_enqueue,
                              test_capability_denials_reject_before_enqueue,
                              test_missing_capability_redacts_unsafe_admission_hints,
                              test_custom_capability_hook_allows_non_database_provider,
                              test_blocking_pool_invalid_config_rejected,
                              test_cancel_timeout_and_late_completion_cleanup_once,
                              test_queued_cancel_prevents_worker_execution,
                              test_shutdown_rejects_new_work_and_cancels_pending,
                              test_blocking_pool_promotes_queued_when_slot_frees,
                              test_blocking_pool_workers_cap_parallel_execution_and_fifo_queue,
                              test_blocking_pool_overflow_shutdown_and_cleanup_once,
                              test_serialized_worker_executes_one_at_a_time_fifo,
                              test_serialized_worker_capacity_and_reject_ownership,
                              test_serialized_worker_failure_and_late_completion_cleanup_once,
                              test_completion_post_failure_releases_claimed_active_operation,
                              test_shutdown_leaves_worker_claimed_operation_for_worker_completion,
                              test_shutdown_of_queued_operation_preserves_active_in_flight,
                              test_serialized_dispose_stops_zero_capacity_worker,
                              test_serialized_run_requires_thread_safe_async_backend,
                              test_per_instance_isolation};
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        int result = tests[index]();
        if (result != 0) {
            return result;
        }
    }

    return 0;
}
