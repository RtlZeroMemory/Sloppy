#ifndef SLOPPY_PROVIDER_EXECUTOR_H
#define SLOPPY_PROVIDER_EXECUTOR_H

#include "sloppy/async_backend.h"
#include "sloppy/bytes.h"
#include "sloppy/cancellation.h"
#include "sloppy/diagnostics.h"
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

typedef enum SlProviderExecutorShutdownPolicy
{
    SL_PROVIDER_EXECUTOR_SHUTDOWN_IMMEDIATE_CANCEL = 0
} SlProviderExecutorShutdownPolicy;

typedef void (*SlProviderOperationCleanupFn)(SlProviderOperation* operation, void* user);

typedef struct SlProviderExecutorConfig
{
    SlStr instance_id;
    SlProviderExecutionMode mode;
    size_t queue_capacity;
    size_t worker_count;
} SlProviderExecutorConfig;

/*
 * Borrowed submission inputs for one provider/offload operation.
 *
 * sl_provider_executor_submit() copies the textual metadata and input bytes into the
 * caller-provided arena before the operation can outlive the caller stack. `cancellation`
 * is borrowed and must outlive the operation when supplied. Completion dispatch runs only
 * when the owning SlAsyncLoop drains the terminal completion. Cleanup runs exactly once
 * after terminal dispatch or explicit discard.
 */
typedef struct SlProviderOperationDescriptor
{
    SlStr provider_instance_id;
    SlStr provider_kind;
    SlStr operation_name;
    SlStr capability_token;
    SlProviderExecutionMode execution_mode;
    SlCancellationToken* cancellation;
    SlBytes input;
    SlAsyncCompletionDispatchFn completion_dispatch;
    void* completion_dispatch_user;
    SlProviderOperationCleanupFn cleanup;
    void* cleanup_user;
} SlProviderOperationDescriptor;

typedef struct SlProviderExecutorSlot
{
    SlProviderOperation* operation;
} SlProviderExecutorSlot;

struct SlProviderOperation
{
    SlProviderInstanceExecutor* executor;
    SlProviderOperationState state;
    SlProviderExecutionMode execution_mode;
    SlCancellationToken* cancellation;
    SlOwnedStr provider_instance_id;
    SlOwnedStr provider_kind;
    SlOwnedStr operation_name;
    SlOwnedStr capability_token;
    SlOwnedBytes input;
    SlStatus terminal_status;
    SlCancellationReason terminal_reason;
    SlDiag diag;
    SlAsyncCompletionDispatchFn completion_dispatch;
    void* completion_dispatch_user;
    SlProviderOperationCleanupFn cleanup;
    void* cleanup_user;
    bool cleanup_ran;
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
    size_t worker_count;
    size_t next_sequence;
    size_t submitted_count;
    size_t completed_count;
    size_t cancelled_count;
    size_t timed_out_count;
    size_t overflow_count;
    size_t shutdown_rejected_count;
    bool shutting_down;
    SlOwnedStr instance_id;
};

SlStr sl_provider_execution_mode_name(SlProviderExecutionMode mode);
SlStatus sl_provider_execution_mode_parse(SlStr text, SlProviderExecutionMode* out);
bool sl_provider_execution_mode_is_supported(SlProviderExecutionMode mode);

/*
 * Initializes a bounded per-provider-instance executor over caller-owned slot storage.
 *
 * The executor does not start worker threads. It models admission, serialized activation,
 * terminal completion posting, and shutdown policy. `arena`, `slots`, and
 * `completion_loop` must outlive the executor.
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
 * On success, `out_operation` receives an arena-owned operation handle. On overflow,
 * shutdown, pre-cancelled tokens, or copy failure, no operation ownership is transferred.
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
