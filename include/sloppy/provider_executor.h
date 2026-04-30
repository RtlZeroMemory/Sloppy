#ifndef SLOPPY_PROVIDER_EXECUTOR_H
#define SLOPPY_PROVIDER_EXECUTOR_H

#include "sloppy/async_backend.h"
#include "sloppy/bytes.h"
#include "sloppy/capability.h"
#include "sloppy/cancellation.h"
#include "sloppy/diagnostics.h"
#include "sloppy/platform_thread.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlProviderInstanceExecutor SlProviderInstanceExecutor;
typedef struct SlProviderOperation SlProviderOperation;

typedef enum SlProviderExecutionMode
{
    SL_PROVIDER_EXECUTION_INLINE_FAST = 0,
    SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING = 1,
    SL_PROVIDER_EXECUTION_BLOCKING_POOL = 2,
    SL_PROVIDER_EXECUTION_NONBLOCKING_IO = 3,
    SL_PROVIDER_EXECUTION_EXTERNAL_MANAGED = 4
} SlProviderExecutionMode;

typedef enum SlProviderOperationState
{
    SL_PROVIDER_OPERATION_EMPTY = 0,
    SL_PROVIDER_OPERATION_QUEUED = 1,
    SL_PROVIDER_OPERATION_ACTIVE = 2,
    SL_PROVIDER_OPERATION_TERMINAL = 3
} SlProviderOperationState;

typedef enum SlProviderOperationKind
{
    SL_PROVIDER_OPERATION_KIND_UNKNOWN = 0,
    SL_PROVIDER_OPERATION_KIND_OPEN = 1,
    SL_PROVIDER_OPERATION_KIND_EXEC = 2,
    SL_PROVIDER_OPERATION_KIND_QUERY = 3,
    SL_PROVIDER_OPERATION_KIND_QUERY_ONE = 4,
    SL_PROVIDER_OPERATION_KIND_CLOSE = 5,
    SL_PROVIDER_OPERATION_KIND_INTERNAL = 6
} SlProviderOperationKind;

typedef enum SlProviderExecutorShutdownPolicy
{
    SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL = 0
} SlProviderExecutorShutdownPolicy;

typedef void (*SlProviderOperationCleanupFn)(SlProviderOperation* operation, void* user);
typedef SlStatus (*SlProviderOperationRunFn)(SlProviderOperation* operation, void* user,
                                             SlDiagCode* out_diag_code, SlStr* out_message);
typedef SlStatus (*SlProviderCapabilityCheckFn)(const SlCapabilityRegistry* registry,
                                                SlArena* diag_arena, SlStr token,
                                                SlCapabilityOperation operation,
                                                SlStr provider_token, SlStr provider_kind,
                                                SlDiag* out_diag, void* user);

typedef struct SlProviderCapabilityRequirement
{
    SlStr token;
    SlCapabilityOperation operation;
} SlProviderCapabilityRequirement;

typedef struct SlProviderExecutorConfig
{
    SlStr instance_id;
    SlStr provider_kind;
    SlStr provider_token;
    SlProviderExecutionMode mode;
    size_t queue_capacity;
    size_t worker_count;
    size_t max_in_flight;
    const SlCapabilityRegistry* capability_registry;
    SlProviderCapabilityCheckFn capability_check;
    void* capability_check_user;
    void* app_owner;
    void* config_binding;
} SlProviderExecutorConfig;

/*
 * Borrowed submission inputs for one provider/offload operation.
 *
 * sl_provider_executor_submit() copies the textual metadata and input bytes into the
 * caller-provided arena before the operation can outlive the caller stack. `cancellation`,
 * `deadline`, and pointer metadata are borrowed placeholders and must outlive the
 * operation unless their owning scope is retained through `scope`. Completion dispatch
 * runs only when the owning SlAsyncLoop drains the terminal completion. Cleanup runs
 * exactly once after terminal dispatch or explicit discard.
 *
 * Failed admission does not transfer ownership and does not call `cleanup`; callers retain
 * responsibility for any submission-side storage they own.
 */
typedef struct SlProviderOperationDescriptor
{
    void* request_or_app_scope;
    SlCancellationToken* cancellation;
    void* deadline;
    SlAsyncCompletionDispatchFn completion_dispatch;
    void* completion_dispatch_user;
    SlProviderOperationRunFn run;
    void* run_user;
    SlProviderOperationCleanupFn cleanup;
    void* cleanup_user;
    SlDiag* admission_diag;
    SlStr provider_instance_id;
    SlStr provider_kind;
    SlStr operation_name;
    SlBytes input;
    SlStr diagnostic_context;
    SlProviderCapabilityRequirement capability;
    SlAsyncScopeRef scope;
    SlProviderOperationKind operation_kind;
    SlProviderExecutionMode execution_mode;
} SlProviderOperationDescriptor;

typedef struct SlProviderExecutorSlot
{
    SlProviderOperation* operation;
} SlProviderExecutorSlot;

struct SlProviderOperation
{
    SlProviderInstanceExecutor* executor;
    SlProviderOperationState state;
    SlProviderOperationKind operation_kind;
    SlProviderExecutionMode execution_mode;
    void* request_or_app_scope;
    SlAsyncScopeRef scope;
    SlCancellationToken* cancellation;
    void* deadline;
    SlOwnedStr provider_instance_id;
    SlOwnedStr provider_kind;
    SlOwnedStr operation_name;
    SlProviderCapabilityRequirement capability;
    SlOwnedStr capability_token;
    SlOwnedStr diagnostic_context;
    SlOwnedBytes input;
    SlStatus terminal_status;
    SlCancellationReason terminal_reason;
    SlDiag diag;
    SlProviderOperationRunFn run;
    void* run_user;
    SlAsyncCompletionDispatchFn completion_dispatch;
    void* completion_dispatch_user;
    SlProviderOperationCleanupFn cleanup;
    void* cleanup_user;
    bool cleanup_ran;
    bool worker_claimed;
    bool terminal_completion_drained;
    bool counted_in_flight;
    size_t sequence;
};

struct SlProviderInstanceExecutor
{
    SlProviderExecutionMode mode;
    SlAsyncLoop* completion_loop;
    SlProviderExecutorSlot* slots;
    size_t capacity;
    size_t count;
    size_t in_flight;
    size_t max_in_flight;
    size_t worker_count;
    size_t next_sequence;
    size_t submitted_count;
    size_t completed_count;
    size_t cancelled_count;
    size_t timed_out_count;
    size_t overflow_count;
    size_t shutdown_rejected_count;
    size_t worker_started_count;
    size_t worker_stopped_count;
    size_t worker_failure_count;
    size_t completion_post_failure_count;
    size_t late_completion_count;
    bool shutting_down;
    bool worker_stop_requested;
    bool worker_running;
    SlPlatformMutex* mutex;
    SlPlatformCond* worker_cond;
    SlPlatformThread** worker_threads;
    SlPlatformThread* worker_thread;
    SlOwnedStr instance_id;
    SlOwnedStr provider_kind;
    SlOwnedStr provider_token;
    const SlCapabilityRegistry* capability_registry;
    SlProviderCapabilityCheckFn capability_check;
    void* capability_check_user;
    void* app_owner;
    void* config_binding;
};

SlStr sl_provider_execution_mode_name(SlProviderExecutionMode mode);
SlStatus sl_provider_execution_mode_parse(SlStr text, SlProviderExecutionMode* out);
bool sl_provider_execution_mode_is_supported(SlProviderExecutionMode mode);
SlStr sl_provider_operation_kind_name(SlProviderOperationKind kind);
bool sl_provider_operation_kind_is_supported(SlProviderOperationKind kind);

SlProviderOperationDescriptor sl_provider_operation_descriptor_init(
    SlStr provider_instance_id, SlStr provider_kind, SlProviderOperationKind operation_kind,
    SlStr operation_name, SlProviderExecutionMode execution_mode,
    SlAsyncCompletionDispatchFn completion_dispatch, void* completion_dispatch_user);
SlStatus sl_provider_operation_descriptor_set_input(SlProviderOperationDescriptor* descriptor,
                                                    SlBytes input);
SlStatus
sl_provider_operation_descriptor_attach_capability(SlProviderOperationDescriptor* descriptor,
                                                   SlStr token, SlCapabilityOperation operation);
SlStatus sl_provider_operation_descriptor_attach_cancellation(
    SlProviderOperationDescriptor* descriptor, SlCancellationToken* cancellation, void* deadline);
SlStatus sl_provider_operation_descriptor_attach_scope(SlProviderOperationDescriptor* descriptor,
                                                       void* request_or_app_scope,
                                                       SlAsyncScopeRef scope);
SlStatus sl_provider_operation_descriptor_attach_cleanup(SlProviderOperationDescriptor* descriptor,
                                                         SlProviderOperationCleanupFn cleanup,
                                                         void* user);
SlStatus
sl_provider_operation_descriptor_attach_admission_diag(SlProviderOperationDescriptor* descriptor,
                                                       SlDiag* out_diag);
SlStatus sl_provider_operation_descriptor_attach_run(SlProviderOperationDescriptor* descriptor,
                                                     SlProviderOperationRunFn run, void* user);
SlStatus
sl_provider_operation_descriptor_set_diagnostic_context(SlProviderOperationDescriptor* descriptor,
                                                        SlStr diagnostic_context);

/*
 * Initializes a bounded per-provider-instance executor over caller-owned slot storage.
 *
 * `config->capability_check` is required. The executor owns admission timing, but the
 * caller supplies provider-specific policy so database, filesystem, network, and future
 * native-addon providers can share the worker model without SQL-specific coupling here.
 * `config->capability_registry` is borrowed for the executor lifetime when the supplied
 * policy hook needs registry-backed Plan metadata.
 * `config->provider_token` names the provider capability/policy token checked against
 * submitted operation capabilities; when empty, the provider instance id is used as the
 * token.
 *
 * SERIALIZED_BLOCKING starts one long-lived worker on init. BLOCKING_POOL starts a bounded
 * number of long-lived workers on init and caps active work to configured max_in_flight,
 * which must not exceed worker_count. Other modes currently model per-provider-instance
 * admission, mode metadata, pending/in-flight counts, terminal completion posting, and
 * shutdown policy without execution engines. `arena`, `slots`, and `completion_loop` must
 * outlive the executor. Provider work admitted here must never block the V8 owner thread,
 * and workers must never enter V8.
 *
 * Threading contract: this executor is caller-serialized. `init`, `dispose`, `submit`,
 * `complete`, `cancel`, `timeout`, `shutdown`, and query calls must not race with each
 * other on the same executor or operation. The caller owns any required synchronization,
 * descriptor-memory visibility before submission, and executor lifetime.
 */
SlStatus sl_provider_executor_init(SlProviderInstanceExecutor* executor, SlArena* arena,
                                   const SlProviderExecutorConfig* config,
                                   SlProviderExecutorSlot* slots, SlAsyncLoop* completion_loop);
void sl_provider_executor_dispose(SlProviderInstanceExecutor* executor);

/*
 * Submits one provider operation if capacity and shutdown/cancellation state allow it.
 *
 * Capability denial, overflow, shutdown, pre-cancelled tokens, expired-deadline tokens, or
 * copy failure reject before ownership transfer. On success, `out_operation` receives an
 * arena-owned operation handle. If the descriptor has an attached admission diagnostic,
 * capability denials write redacted diagnostic context into that caller-owned storage.
 */
SlStatus sl_provider_executor_submit(SlProviderInstanceExecutor* executor, SlArena* arena,
                                     const SlProviderOperationDescriptor* descriptor,
                                     SlProviderOperation** out_operation);
SlStatus sl_provider_operation_complete(SlProviderOperation* operation, SlStatus status,
                                        SlDiagCode diag_code, SlStr message);
SlStatus sl_provider_operation_cancel(SlProviderOperation* operation, SlStr detail);
SlStatus sl_provider_operation_timeout(SlProviderOperation* operation, SlStr detail);
/*
 * Starts executor shutdown. The current policy is immediate cancellation: new work is
 * rejected and pending/active operations post shutdown terminal completions.
 */
SlStatus sl_provider_executor_shutdown(SlProviderInstanceExecutor* executor,
                                       SlProviderExecutorShutdownPolicy policy);

SlProviderOperationState sl_provider_operation_state(const SlProviderOperation* operation);
SlBytes sl_provider_operation_input(const SlProviderOperation* operation);
size_t sl_provider_executor_pending_count(const SlProviderInstanceExecutor* executor);
size_t sl_provider_executor_in_flight_count(const SlProviderInstanceExecutor* executor);
bool sl_provider_executor_is_shutting_down(const SlProviderInstanceExecutor* executor);

#ifdef __cplusplus
}
#endif

#endif
