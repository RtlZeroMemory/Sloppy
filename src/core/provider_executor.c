/*
 * src/core/provider_executor.c
 *
 * Implements Sloppy's bounded provider/offload executor model. This is the deterministic
 * ENGINE-23.AB foundation: it owns queued operation inputs and posts terminal completions
 * through SlAsyncLoop, but it does not start production provider worker threads.
 */
#include "sloppy/provider_executor.h"

#include "sloppy/assert.h"

static bool sl_provider_str_valid(SlStr value)
{
    return value.length == 0U || value.ptr != NULL;
}

static SlStatus sl_provider_copy_str(SlArena* arena, SlStr input, SlOwnedStr* out)
{
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (input.length == 0U) {
        *out = (SlOwnedStr){0};
        return sl_status_ok();
    }

    return sl_str_copy_to_arena(arena, input, out);
}

SlStr sl_provider_execution_mode_name(SlProviderExecutionMode mode)
{
    switch (mode) {
    case SL_PROVIDER_EXECUTION_INLINE_FAST:
        return sl_str_from_cstr("InlineFast");
    case SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING:
        return sl_str_from_cstr("SerializedBlocking");
    case SL_PROVIDER_EXECUTION_BLOCKING_POOL:
        return sl_str_from_cstr("BlockingPool");
    case SL_PROVIDER_EXECUTION_NONBLOCKING_IO:
        return sl_str_from_cstr("NonBlockingIo");
    case SL_PROVIDER_EXECUTION_EXTERNAL_MANAGED:
        return sl_str_from_cstr("ExternalManaged");
    default:
        return sl_str_from_cstr("Unknown");
    }
}

SlStatus sl_provider_execution_mode_parse(SlStr text, SlProviderExecutionMode* out)
{
    if (out == NULL || !sl_provider_str_valid(text)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_str_equal(text, sl_str_from_cstr("InlineFast")) ||
        sl_str_equal(text, sl_str_from_cstr("inline-fast")))
    {
        *out = SL_PROVIDER_EXECUTION_INLINE_FAST;
        return sl_status_ok();
    }
    if (sl_str_equal(text, sl_str_from_cstr("SerializedBlocking")) ||
        sl_str_equal(text, sl_str_from_cstr("serialized-blocking")))
    {
        *out = SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING;
        return sl_status_ok();
    }
    if (sl_str_equal(text, sl_str_from_cstr("BlockingPool")) ||
        sl_str_equal(text, sl_str_from_cstr("blocking-pool")))
    {
        *out = SL_PROVIDER_EXECUTION_BLOCKING_POOL;
        return sl_status_ok();
    }
    if (sl_str_equal(text, sl_str_from_cstr("NonBlockingIo")) ||
        sl_str_equal(text, sl_str_from_cstr("nonblocking-io")))
    {
        *out = SL_PROVIDER_EXECUTION_NONBLOCKING_IO;
        return sl_status_ok();
    }
    if (sl_str_equal(text, sl_str_from_cstr("ExternalManaged")) ||
        sl_str_equal(text, sl_str_from_cstr("external-managed")))
    {
        *out = SL_PROVIDER_EXECUTION_EXTERNAL_MANAGED;
        return sl_status_ok();
    }

    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

bool sl_provider_execution_mode_is_supported(SlProviderExecutionMode mode)
{
    return mode == SL_PROVIDER_EXECUTION_INLINE_FAST ||
           mode == SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING ||
           mode == SL_PROVIDER_EXECUTION_BLOCKING_POOL ||
           mode == SL_PROVIDER_EXECUTION_NONBLOCKING_IO ||
           mode == SL_PROVIDER_EXECUTION_EXTERNAL_MANAGED;
}

SlStr sl_provider_operation_kind_name(SlProviderOperationKind kind)
{
    switch (kind) {
    case SL_PROVIDER_OPERATION_KIND_OPEN:
        return sl_str_from_cstr("open");
    case SL_PROVIDER_OPERATION_KIND_EXEC:
        return sl_str_from_cstr("exec");
    case SL_PROVIDER_OPERATION_KIND_QUERY:
        return sl_str_from_cstr("query");
    case SL_PROVIDER_OPERATION_KIND_QUERY_ONE:
        return sl_str_from_cstr("queryOne");
    case SL_PROVIDER_OPERATION_KIND_CLOSE:
        return sl_str_from_cstr("close");
    case SL_PROVIDER_OPERATION_KIND_INTERNAL:
        return sl_str_from_cstr("internal");
    default:
        return sl_str_from_cstr("unknown");
    }
}

bool sl_provider_operation_kind_is_supported(SlProviderOperationKind kind)
{
    return kind == SL_PROVIDER_OPERATION_KIND_OPEN || kind == SL_PROVIDER_OPERATION_KIND_EXEC ||
           kind == SL_PROVIDER_OPERATION_KIND_QUERY ||
           kind == SL_PROVIDER_OPERATION_KIND_QUERY_ONE ||
           kind == SL_PROVIDER_OPERATION_KIND_CLOSE || kind == SL_PROVIDER_OPERATION_KIND_INTERNAL;
}

SlProviderOperationDescriptor sl_provider_operation_descriptor_init(
    SlStr provider_instance_id, SlStr provider_kind, SlProviderOperationKind operation_kind,
    SlStr operation_name, SlProviderExecutionMode execution_mode,
    SlAsyncCompletionDispatchFn completion_dispatch, void* completion_dispatch_user)
{
    SlProviderOperationDescriptor descriptor = {0};

    descriptor.provider_instance_id = provider_instance_id;
    descriptor.provider_kind = provider_kind;
    descriptor.operation_kind = operation_kind;
    descriptor.operation_name = operation_name;
    descriptor.execution_mode = execution_mode;
    descriptor.completion_dispatch = completion_dispatch;
    descriptor.completion_dispatch_user = completion_dispatch_user;
    return descriptor;
}

SlStatus sl_provider_operation_descriptor_set_input(SlProviderOperationDescriptor* descriptor,
                                                    SlBytes input)
{
    if (descriptor == NULL || (input.length != 0U && input.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    descriptor->input = input;
    return sl_status_ok();
}

SlStatus
sl_provider_operation_descriptor_attach_capability(SlProviderOperationDescriptor* descriptor,
                                                   SlStr token, SlCapabilityOperation operation)
{
    if (descriptor == NULL || !sl_provider_str_valid(token) ||
        operation < SL_CAPABILITY_OPERATION_READ || operation > SL_CAPABILITY_OPERATION_LISTEN)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    descriptor->capability.token = token;
    descriptor->capability.operation = operation;
    return sl_status_ok();
}

SlStatus sl_provider_operation_descriptor_attach_cancellation(
    SlProviderOperationDescriptor* descriptor, SlCancellationToken* cancellation, void* deadline)
{
    if (descriptor == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    descriptor->cancellation = cancellation;
    descriptor->deadline = deadline;
    return sl_status_ok();
}

SlStatus sl_provider_operation_descriptor_attach_scope(SlProviderOperationDescriptor* descriptor,
                                                       void* request_or_app_scope,
                                                       SlAsyncScopeRef scope)
{
    if (descriptor == NULL ||
        (scope.scope == NULL && (scope.retain != NULL || scope.release != NULL)))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    descriptor->request_or_app_scope = request_or_app_scope;
    descriptor->scope = scope;
    return sl_status_ok();
}

SlStatus sl_provider_operation_descriptor_attach_cleanup(SlProviderOperationDescriptor* descriptor,
                                                         SlProviderOperationCleanupFn cleanup,
                                                         void* user)
{
    if (descriptor == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    descriptor->cleanup = cleanup;
    descriptor->cleanup_user = user;
    return sl_status_ok();
}

SlStatus
sl_provider_operation_descriptor_set_diagnostic_context(SlProviderOperationDescriptor* descriptor,
                                                        SlStr diagnostic_context)
{
    if (descriptor == NULL || !sl_provider_str_valid(diagnostic_context)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    descriptor->diagnostic_context = diagnostic_context;
    return sl_status_ok();
}

static SlProviderExecutorSlot*
sl_provider_executor_find_free_slot(SlProviderInstanceExecutor* executor)
{
    size_t index = 0U;

    if (executor == NULL || executor->slots == NULL) {
        return NULL;
    }

    for (index = 0U; index < executor->capacity; index += 1U) {
        if (executor->slots[index].operation == NULL) {
            return &executor->slots[index];
        }
    }

    return NULL;
}

static SlProviderExecutorSlot*
sl_provider_executor_find_operation_slot(SlProviderInstanceExecutor* executor,
                                         const SlProviderOperation* operation)
{
    size_t index = 0U;

    if (executor == NULL || operation == NULL || executor->slots == NULL) {
        return NULL;
    }

    for (index = 0U; index < executor->capacity; index += 1U) {
        if (executor->slots[index].operation == operation) {
            return &executor->slots[index];
        }
    }

    return NULL;
}

static bool sl_provider_sequence_before(size_t candidate, size_t current)
{
    const size_t max_size = ~(size_t)0;
    const size_t half_range = max_size / 2U;
    const size_t diff = candidate - current;

    return candidate != current && diff > half_range;
}

static SlProviderOperation*
sl_provider_executor_next_queued(const SlProviderInstanceExecutor* executor)
{
    SlProviderOperation* best = NULL;
    size_t index = 0U;

    if (executor == NULL || executor->slots == NULL) {
        return NULL;
    }

    for (index = 0U; index < executor->capacity; index += 1U) {
        SlProviderOperation* candidate = executor->slots[index].operation;
        if (candidate != NULL && candidate->state == SL_PROVIDER_OPERATION_QUEUED &&
            (best == NULL || sl_provider_sequence_before(candidate->sequence, best->sequence)))
        {
            best = candidate;
        }
    }

    return best;
}

static void sl_provider_executor_activate_next(SlProviderInstanceExecutor* executor)
{
    SlProviderOperation* next = NULL;

    if (executor == NULL) {
        return;
    }

    while (executor->in_flight < executor->max_in_flight) {
        next = sl_provider_executor_next_queued(executor);
        if (next == NULL) {
            return;
        }
        next->state = SL_PROVIDER_OPERATION_ACTIVE;
        executor->in_flight += 1U;
    }
}

static size_t sl_provider_executor_default_max_in_flight(SlProviderExecutionMode mode,
                                                         size_t configured_max, size_t worker_count)
{
    if (configured_max != 0U) {
        return configured_max;
    }

    if (mode == SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING) {
        return 1U;
    }

    if (mode == SL_PROVIDER_EXECUTION_BLOCKING_POOL && worker_count != 0U) {
        return worker_count;
    }

    return 1U;
}

SlStatus sl_provider_executor_init(SlProviderInstanceExecutor* executor, SlArena* arena,
                                   const SlProviderExecutorConfig* config,
                                   SlProviderExecutorSlot* slots, SlAsyncLoop* completion_loop)
{
    size_t index = 0U;
    SlStatus status;

    if (executor == NULL || arena == NULL || config == NULL || completion_loop == NULL ||
        !sl_provider_execution_mode_is_supported(config->mode) ||
        config->instance_id.length == 0U || config->provider_kind.length == 0U ||
        (slots == NULL && config->queue_capacity != 0U) ||
        !sl_provider_str_valid(config->instance_id) ||
        !sl_provider_str_valid(config->provider_kind))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *executor = (SlProviderInstanceExecutor){0};
    executor->mode = config->mode;
    executor->completion_loop = completion_loop;
    executor->slots = slots;
    executor->capacity = config->queue_capacity;
    executor->worker_count = config->worker_count;
    executor->max_in_flight = sl_provider_executor_default_max_in_flight(
        config->mode, config->max_in_flight, config->worker_count);
    executor->app_owner = config->app_owner;
    executor->config_binding = config->config_binding;
    status = sl_provider_copy_str(arena, config->instance_id, &executor->instance_id);
    if (sl_status_is_ok(status)) {
        status = sl_provider_copy_str(arena, config->provider_kind, &executor->provider_kind);
    }
    if (!sl_status_is_ok(status)) {
        *executor = (SlProviderInstanceExecutor){0};
        return status;
    }

    for (index = 0U; index < config->queue_capacity; index += 1U) {
        slots[index] = (SlProviderExecutorSlot){0};
    }

    return sl_status_ok();
}

static void sl_provider_operation_run_cleanup_once(SlProviderOperation* operation)
{
    if (operation == NULL || operation->cleanup_ran) {
        return;
    }

    operation->cleanup_ran = true;
    if (operation->cleanup != NULL) {
        operation->cleanup(operation, operation->cleanup_user);
    }
}

static void sl_provider_operation_discard(SlProviderOperation* operation,
                                          SlCancellationReason reason)
{
    if (operation == NULL) {
        return;
    }

    operation->state = SL_PROVIDER_OPERATION_TERMINAL;
    operation->terminal_reason = reason;
    operation->terminal_status = sl_status_from_code(sl_cancellation_status_code(reason));
    sl_provider_operation_run_cleanup_once(operation);
}

void sl_provider_executor_dispose(SlProviderInstanceExecutor* executor)
{
    size_t index = 0U;

    if (executor == NULL || executor->slots == NULL) {
        return;
    }

    executor->shutting_down = true;
    for (index = 0U; index < executor->capacity; index += 1U) {
        SlProviderOperation* operation = executor->slots[index].operation;
        if (operation != NULL) {
            sl_provider_operation_discard(operation, SL_CANCELLATION_REASON_SHUTDOWN);
            executor->slots[index].operation = NULL;
        }
    }
    executor->count = 0U;
    executor->in_flight = 0U;
}

static SlDiag sl_provider_diag(SlDiagCode code, SlStr message)
{
    SlDiag diag = {0};

    diag.severity = SL_DIAG_SEVERITY_ERROR;
    diag.code = code;
    diag.message = message;
    diag.primary_span = sl_source_span_unknown();
    return diag;
}

static bool sl_provider_descriptor_valid(const SlProviderOperationDescriptor* descriptor)
{
    return descriptor != NULL && descriptor->completion_dispatch != NULL &&
           sl_provider_execution_mode_is_supported(descriptor->execution_mode) &&
           sl_provider_operation_kind_is_supported(descriptor->operation_kind) &&
           descriptor->provider_instance_id.length != 0U &&
           descriptor->provider_kind.length != 0U && descriptor->operation_name.length != 0U &&
           sl_provider_str_valid(descriptor->provider_instance_id) &&
           sl_provider_str_valid(descriptor->provider_kind) &&
           sl_provider_str_valid(descriptor->operation_name) &&
           sl_provider_str_valid(descriptor->capability.token) &&
           sl_provider_str_valid(descriptor->diagnostic_context) &&
           (descriptor->input.length == 0U || descriptor->input.ptr != NULL);
}

static void
sl_provider_operation_init_from_descriptor(SlProviderOperation* operation,
                                           SlProviderInstanceExecutor* executor,
                                           const SlProviderOperationDescriptor* descriptor)
{
    *operation = (SlProviderOperation){0};
    operation->executor = executor;
    operation->state = SL_PROVIDER_OPERATION_QUEUED;
    operation->operation_kind = descriptor->operation_kind;
    operation->execution_mode = descriptor->execution_mode;
    operation->request_or_app_scope = descriptor->request_or_app_scope;
    operation->scope = descriptor->scope;
    operation->cancellation = descriptor->cancellation;
    operation->deadline = descriptor->deadline;
    operation->terminal_status = sl_status_ok();
    operation->completion_dispatch = descriptor->completion_dispatch;
    operation->completion_dispatch_user = descriptor->completion_dispatch_user;
    operation->cleanup = descriptor->cleanup;
    operation->cleanup_user = descriptor->cleanup_user;
    operation->sequence = executor->next_sequence;
    executor->next_sequence += 1U;
}

static SlStatus
sl_provider_operation_copy_descriptor(SlArena* arena, SlProviderOperation* operation,
                                      const SlProviderOperationDescriptor* descriptor)
{
    SlStatus status = sl_provider_copy_str(arena, descriptor->provider_instance_id,
                                           &operation->provider_instance_id);

    if (sl_status_is_ok(status)) {
        status = sl_provider_copy_str(arena, descriptor->provider_kind, &operation->provider_kind);
    }
    if (sl_status_is_ok(status)) {
        status =
            sl_provider_copy_str(arena, descriptor->operation_name, &operation->operation_name);
    }
    if (sl_status_is_ok(status)) {
        status =
            sl_provider_copy_str(arena, descriptor->capability.token, &operation->capability_token);
    }
    if (sl_status_is_ok(status)) {
        operation->capability.token = sl_owned_str_as_view(operation->capability_token);
        operation->capability.operation = descriptor->capability.operation;
        status = sl_provider_copy_str(arena, descriptor->diagnostic_context,
                                      &operation->diagnostic_context);
    }
    if (sl_status_is_ok(status)) {
        status = sl_bytes_copy_to_arena(arena, descriptor->input, &operation->input);
    }

    return status;
}

static void sl_provider_executor_activate_submitted(SlProviderInstanceExecutor* executor,
                                                    SlProviderOperation* operation)
{
    (void)operation;
    sl_provider_executor_activate_next(executor);
}

SlStatus sl_provider_executor_submit(SlProviderInstanceExecutor* executor, SlArena* arena,
                                     const SlProviderOperationDescriptor* descriptor,
                                     SlProviderOperation** out_operation)
{
    SlProviderExecutorSlot* slot = NULL;
    SlProviderOperation* operation = NULL;
    void* memory = NULL;
    SlArenaMark mark;
    SlStatus status;

    if (out_operation != NULL) {
        *out_operation = NULL;
    }

    if (executor == NULL || arena == NULL || out_operation == NULL ||
        !sl_provider_descriptor_valid(descriptor) || descriptor->execution_mode != executor->mode)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_str_equal(descriptor->provider_instance_id,
                      sl_owned_str_as_view(executor->instance_id)) ||
        !sl_str_equal(descriptor->provider_kind, sl_owned_str_as_view(executor->provider_kind)))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (executor->shutting_down) {
        executor->shutdown_rejected_count += 1U;
        return sl_status_from_code(SL_STATUS_CANCELLED);
    }

    if (descriptor->cancellation != NULL &&
        sl_cancellation_token_is_cancelled(descriptor->cancellation))
    {
        return sl_status_from_code(
            sl_cancellation_status_code(sl_cancellation_token_reason(descriptor->cancellation)));
    }

    if (executor->count >= executor->capacity) {
        executor->overflow_count += 1U;
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    slot = sl_provider_executor_find_free_slot(executor);
    if (slot == NULL) {
        SL_ASSERT_MSG(false, "provider executor count/slot invariant violated");
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    mark = sl_arena_mark(arena);
    status =
        sl_arena_alloc(arena, sizeof(SlProviderOperation), _Alignof(SlProviderOperation), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    operation = (SlProviderOperation*)memory;
    sl_provider_operation_init_from_descriptor(operation, executor, descriptor);
    status = sl_provider_operation_copy_descriptor(arena, operation, descriptor);
    if (!sl_status_is_ok(status)) {
        operation->state = SL_PROVIDER_OPERATION_TERMINAL;
        (void)sl_arena_reset_to(arena, mark);
        return status;
    }

    slot->operation = operation;
    executor->count += 1U;
    executor->submitted_count += 1U;
    sl_provider_executor_activate_submitted(executor, operation);

    *out_operation = operation;
    return sl_status_ok();
}

static void sl_provider_completion_cleanup(const SlAsyncCompletion* completion, void* user)
{
    SlProviderOperation* operation = (SlProviderOperation*)user;
    SlProviderInstanceExecutor* executor = operation == NULL ? NULL : operation->executor;
    SlProviderExecutorSlot* slot = NULL;

    (void)completion;
    if (operation == NULL || executor == NULL) {
        return;
    }

    sl_provider_operation_run_cleanup_once(operation);
    slot = sl_provider_executor_find_operation_slot(executor, operation);
    if (slot != NULL) {
        slot->operation = NULL;
    }
    /*
     * These guards preserve cleanup-once disposal after unusual completion paths. They are
     * not synchronization; provider executor calls are externally serialized by contract.
     */
    if (executor->count != 0U) {
        executor->count -= 1U;
    }
    if (operation->state == SL_PROVIDER_OPERATION_TERMINAL && executor->in_flight != 0U) {
        executor->in_flight -= 1U;
    }
    executor->completed_count += 1U;
    sl_provider_executor_activate_next(executor);
}

static SlStatus sl_provider_operation_post_terminal(SlProviderOperation* operation, SlStatus status,
                                                    SlCancellationReason reason,
                                                    SlDiagCode diag_code, SlStr message)
{
    SlAsyncCompletion completion;
    SlStatus post_status;

    if (operation == NULL || operation->executor == NULL || !sl_provider_str_valid(message)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (operation->state == SL_PROVIDER_OPERATION_TERMINAL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (operation->cancellation != NULL &&
        sl_cancellation_token_is_cancelled(operation->cancellation) && sl_status_is_ok(status))
    {
        reason = sl_cancellation_token_reason(operation->cancellation);
        status = sl_status_from_code(sl_cancellation_status_code(reason));
        diag_code = reason == SL_CANCELLATION_REASON_BACKPRESSURE ? SL_DIAG_ENGINE_BACKPRESSURE
                                                                  : SL_DIAG_ENGINE_CANCELLED;
        message = sl_cancellation_reason_name(reason);
    }

    operation->terminal_status = status;
    operation->terminal_reason = reason;
    operation->diag = sl_provider_diag(diag_code, message);

    completion = (SlAsyncCompletion){0};
    completion.kind = SL_ASYNC_COMPLETION_NATIVE;
    completion.operation_kind = SL_ASYNC_OPERATION_PROVIDER;
    completion.status = status;
    completion.diag = &operation->diag;
    completion.payload = operation;
    completion.operation = operation;
    completion.scope = operation->scope;
    completion.dispatch = operation->completion_dispatch;
    completion.dispatch_user = operation->completion_dispatch_user;
    completion.cleanup = sl_provider_completion_cleanup;
    completion.cleanup_user = operation;

    post_status = sl_async_loop_post(operation->executor->completion_loop, &completion);
    if (!sl_status_is_ok(post_status)) {
        operation->terminal_status = sl_status_ok();
        operation->terminal_reason = SL_CANCELLATION_REASON_NONE;
        operation->diag = (SlDiag){0};
        return post_status;
    }

    operation->state = SL_PROVIDER_OPERATION_TERMINAL;
    return sl_status_ok();
}

SlStatus sl_provider_operation_complete(SlProviderOperation* operation, SlStatus status,
                                        SlDiagCode diag_code, SlStr message)
{
    if (operation == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_provider_operation_post_terminal(operation, status, SL_CANCELLATION_REASON_NONE,
                                               diag_code, message);
}

SlStatus sl_provider_operation_cancel(SlProviderOperation* operation, SlStr detail)
{
    SlStatus cancel_status = sl_status_ok();
    SlStatus post_status;

    if (operation == NULL || !sl_provider_str_valid(detail)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (operation->cancellation != NULL &&
        !sl_cancellation_token_is_cancelled(operation->cancellation))
    {
        cancel_status = sl_cancellation_token_cancel(operation->cancellation,
                                                     SL_CANCELLATION_REASON_CANCELLED, detail);
    }

    post_status = sl_provider_operation_post_terminal(
        operation, sl_status_from_code(SL_STATUS_CANCELLED), SL_CANCELLATION_REASON_CANCELLED,
        SL_DIAG_ENGINE_CANCELLED, detail);
    if (!sl_status_is_ok(post_status)) {
        return post_status;
    }

    return cancel_status;
}

SlStatus sl_provider_operation_timeout(SlProviderOperation* operation, SlStr detail)
{
    SlStatus cancel_status = sl_status_ok();
    SlStatus post_status;

    if (operation == NULL || !sl_provider_str_valid(detail)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (operation->cancellation != NULL &&
        !sl_cancellation_token_is_cancelled(operation->cancellation))
    {
        cancel_status = sl_cancellation_token_cancel(
            operation->cancellation, SL_CANCELLATION_REASON_DEADLINE_EXCEEDED, detail);
    }

    if (operation->executor != NULL) {
        operation->executor->timed_out_count += 1U;
    }
    post_status = sl_provider_operation_post_terminal(
        operation, sl_status_from_code(SL_STATUS_DEADLINE_EXCEEDED),
        SL_CANCELLATION_REASON_DEADLINE_EXCEEDED, SL_DIAG_ENGINE_PROMISE_PENDING, detail);
    if (!sl_status_is_ok(post_status)) {
        return post_status;
    }

    return cancel_status;
}

SlStatus sl_provider_executor_shutdown(SlProviderInstanceExecutor* executor,
                                       SlProviderExecutorShutdownPolicy policy)
{
    SlStatus first_failure = sl_status_ok();
    size_t index = 0U;

    if (executor == NULL || policy != SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    executor->shutting_down = true;
    for (index = 0U; index < executor->capacity; index += 1U) {
        SlProviderOperation* operation = executor->slots[index].operation;
        if (operation != NULL && operation->state != SL_PROVIDER_OPERATION_TERMINAL) {
            SlStatus status = sl_provider_operation_post_terminal(
                operation, sl_status_from_code(SL_STATUS_CANCELLED),
                SL_CANCELLATION_REASON_SHUTDOWN, SL_DIAG_APP_LIFECYCLE,
                sl_str_from_cstr("provider executor shutdown"));
            if (sl_status_is_ok(status)) {
                executor->cancelled_count += 1U;
            }
            else if (sl_status_is_ok(first_failure)) {
                first_failure = status;
            }
        }
    }

    return first_failure;
}

SlProviderOperationState sl_provider_operation_state(const SlProviderOperation* operation)
{
    return operation == NULL ? SL_PROVIDER_OPERATION_EMPTY : operation->state;
}

SlBytes sl_provider_operation_input(const SlProviderOperation* operation)
{
    return operation == NULL ? sl_bytes_empty() : sl_owned_bytes_as_view(operation->input);
}

size_t sl_provider_executor_pending_count(const SlProviderInstanceExecutor* executor)
{
    return executor == NULL ? 0U : executor->count;
}

size_t sl_provider_executor_in_flight_count(const SlProviderInstanceExecutor* executor)
{
    return executor == NULL ? 0U : executor->in_flight;
}

bool sl_provider_executor_is_shutting_down(const SlProviderInstanceExecutor* executor)
{
    return executor != NULL && executor->shutting_down;
}
