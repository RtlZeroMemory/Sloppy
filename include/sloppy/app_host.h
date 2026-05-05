#ifndef SLOPPY_APP_HOST_H
#define SLOPPY_APP_HOST_H

#include "sloppy/arena.h"
#include "sloppy/cancellation.h"
#include "sloppy/diagnostics.h"
#include "sloppy/plan.h"
#include "sloppy/resource.h"
#include "sloppy/scope.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlAppHostStartupValidation
{
    SlArena* diag_arena;
    bool require_runnable_route;
    size_t max_runnable_routes;
} SlAppHostStartupValidation;

/*
 * Validates the native app graph represented by the parsed Plan before serving.
 *
 * The validator performs no file I/O, engine calls, HTTP parsing, module execution, service
 * activation, provider opening, or V8 work. It checks only metadata that is already native:
 * plan compatibility fields, route-to-handler consistency, duplicate route/name policy,
 * data provider metadata, capability metadata, and duplicate provider service tokens.
 */
SlStatus sl_app_host_validate_startup(const SlPlan* plan, const SlAppHostStartupValidation* options,
                                      SlDiag* out_diag);

typedef enum SlAppLifecycleState
{
    SL_APP_LIFECYCLE_STATE_CREATED = 0,
    SL_APP_LIFECYCLE_STATE_UNINITIALIZED = SL_APP_LIFECYCLE_STATE_CREATED,
    SL_APP_LIFECYCLE_STATE_STARTING = 1,
    SL_APP_LIFECYCLE_STATE_RUNNING = 2,
    SL_APP_LIFECYCLE_STATE_STARTED = SL_APP_LIFECYCLE_STATE_RUNNING,
    SL_APP_LIFECYCLE_STATE_STOPPING = 3,
    SL_APP_LIFECYCLE_STATE_DRAINING = 4,
    SL_APP_LIFECYCLE_STATE_STOPPED = 5,
    SL_APP_LIFECYCLE_STATE_SHUTDOWN = SL_APP_LIFECYCLE_STATE_STOPPED,
    SL_APP_LIFECYCLE_STATE_FAILED = 6
} SlAppLifecycleState;

/*
 * Caller-owned resource cleanup payload for app and request scopes.
 *
 * The payload borrows `table` and stores one JS-safe resource ID. The table and payload must
 * outlive the scope cleanup that owns the registration. Cleanup closes the resource through
 * SlResourceTable and invalidates `id`; it never exposes or logs native pointers.
 * Typed cleanup is a separate opt-in payload so legacy untyped cleanup payloads remain valid.
 */
typedef struct SlAppResourceCleanup
{
    SlResourceTable* table;
    SlResourceId id;
} SlAppResourceCleanup;

typedef struct SlAppTypedResourceCleanup
{
    SlAppResourceCleanup resource;
    SlResourceKind kind;
} SlAppTypedResourceCleanup;

typedef struct SlAppLifecycle
{
    SlScope cleanups;
    SlAppLifecycleState state;
    uint64_t app_id;
    size_t active_request_scopes;
    size_t late_completion_count;
} SlAppLifecycle;

typedef struct SlAppLifecycleSnapshot
{
    SlAppLifecycleState state;
    uint64_t app_id;
    size_t active_app_scopes;
    size_t active_request_scopes;
    size_t app_cleanup_count;
    size_t cleanup_count;
    size_t late_completion_count;
    size_t leaked_resource_count;
    size_t active_timer_count;
    size_t active_callback_count;
    size_t active_provider_operation_count;
} SlAppLifecycleSnapshot;

/*
 * Starts an app lifecycle over caller-owned cleanup storage.
 *
 * The lifecycle owns cleanup registrations only. Callback payloads remain caller-owned and
 * must outlive shutdown. Startup transitions through STARTING to RUNNING. Shutdown rejects
 * new request scopes, drains already-open request scopes when callers use begin/finish, and
 * closes app cleanups in deterministic LIFO order. Shutdown is idempotent after success and
 * safe to call on a zero-initialized lifecycle.
 */
SlStatus sl_app_lifecycle_start(SlAppLifecycle* lifecycle, SlScopeCleanup* storage,
                                size_t cleanup_capacity, SlDiag* out_diag);
SlStatus sl_app_lifecycle_start_with_id(SlAppLifecycle* lifecycle, SlScopeCleanup* storage,
                                        size_t cleanup_capacity, uint64_t app_id, SlDiag* out_diag);
SlStatus sl_app_lifecycle_add_cleanup(SlAppLifecycle* lifecycle, SlScopeCleanupFn fn, void* payload,
                                      void* user, SlDiag* out_diag);
SlStatus sl_app_lifecycle_add_resource_cleanup(SlAppLifecycle* lifecycle,
                                               SlAppResourceCleanup* resource, SlDiag* out_diag);
SlStatus sl_app_lifecycle_add_typed_resource_cleanup(SlAppLifecycle* lifecycle,
                                                     SlAppTypedResourceCleanup* resource,
                                                     SlDiag* out_diag);
SlStatus sl_app_lifecycle_fail_startup(SlAppLifecycle* lifecycle, SlDiag* out_diag);
SlStatus sl_app_lifecycle_begin_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag);
SlStatus sl_app_lifecycle_finish_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag);
SlStatus sl_app_lifecycle_force_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag);
SlStatus sl_app_lifecycle_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag);
SlAppLifecycleState sl_app_lifecycle_state(const SlAppLifecycle* lifecycle);
bool sl_app_lifecycle_is_started(const SlAppLifecycle* lifecycle);
bool sl_app_lifecycle_is_shutdown(const SlAppLifecycle* lifecycle);
uint64_t sl_app_lifecycle_app_id(const SlAppLifecycle* lifecycle);
size_t sl_app_lifecycle_active_request_count(const SlAppLifecycle* lifecycle);
SlAppLifecycleSnapshot sl_app_lifecycle_snapshot(const SlAppLifecycle* lifecycle);
SlStatus sl_app_lifecycle_assert_no_leaks(const SlAppLifecycle* lifecycle, SlDiag* out_diag);

typedef struct SlAppRequestScope
{
    SlScope cleanups;
    SlAppLifecycle* lifecycle;
    SlStatus terminal_status;
    SlDiagCode terminal_diag_code;
    uint64_t app_id;
    uint64_t request_id;
    SlCancellationReason terminal_reason;
    size_t late_completion_count;
    bool active;
    bool terminal;
} SlAppRequestScope;

typedef struct SlAppRequestScopeSnapshot
{
    uint64_t app_id;
    uint64_t request_id;
    size_t active_request_scopes;
    size_t request_cleanup_count;
    size_t cleanup_count;
    size_t late_completion_count;
    size_t leaked_resource_count;
    bool active;
    bool terminal;
    SlStatus terminal_status;
    SlDiagCode terminal_diag_code;
    SlCancellationReason terminal_reason;
} SlAppRequestScopeSnapshot;

typedef enum SlAppRequestOutcome
{
    SL_APP_REQUEST_OUTCOME_NONE = 0,
    SL_APP_REQUEST_OUTCOME_SUCCESS = 1,
    SL_APP_REQUEST_OUTCOME_SYNC_ERROR = 2,
    SL_APP_REQUEST_OUTCOME_V8_EXCEPTION = 3,
    SL_APP_REQUEST_OUTCOME_PROMISE_REJECTION = 4,
    SL_APP_REQUEST_OUTCOME_VALIDATION_FAILURE = 5,
    SL_APP_REQUEST_OUTCOME_BODY_PARSE_FAILURE = 6,
    SL_APP_REQUEST_OUTCOME_TIMEOUT = 7,
    SL_APP_REQUEST_OUTCOME_CANCELLED = 8,
    SL_APP_REQUEST_OUTCOME_CLIENT_DISCONNECT = 9,
    SL_APP_REQUEST_OUTCOME_RESPONSE_WRITE_FAILURE = 10,
    SL_APP_REQUEST_OUTCOME_PROVIDER_FAILURE = 11,
    SL_APP_REQUEST_OUTCOME_PROVIDER_CANCELLED_BEFORE_START = 12,
    SL_APP_REQUEST_OUTCOME_PROVIDER_LATE_COMPLETION = 13,
    SL_APP_REQUEST_OUTCOME_SHUTDOWN = 14,
    SL_APP_REQUEST_OUTCOME_BACKPRESSURE = 15
} SlAppRequestOutcome;

typedef SlStatus (*SlAppRequestScopeHandler)(SlAppRequestScope* request_scope, void* user,
                                             SlDiag* out_diag);

SlStatus sl_app_request_scope_init(SlAppRequestScope* request_scope, SlScopeCleanup* storage,
                                   size_t cleanup_capacity);
SlStatus sl_app_request_scope_init_for_app(SlAppRequestScope* request_scope,
                                           SlAppLifecycle* lifecycle, uint64_t request_id,
                                           SlScopeCleanup* storage, size_t cleanup_capacity,
                                           SlDiag* out_diag);
SlStatus sl_app_request_scope_add_cleanup(SlAppRequestScope* request_scope, SlScopeCleanupFn fn,
                                          void* payload, void* user);
SlStatus sl_app_request_scope_add_resource_cleanup(SlAppRequestScope* request_scope,
                                                   SlAppResourceCleanup* resource);
SlStatus sl_app_request_scope_add_typed_resource_cleanup(SlAppRequestScope* request_scope,
                                                         SlAppTypedResourceCleanup* resource);
SlStatus sl_app_request_scope_complete(SlAppRequestScope* request_scope,
                                       SlAppRequestOutcome outcome, SlStatus status,
                                       SlDiagCode diag_code, SlDiag* out_diag);
SlStatus sl_app_request_scope_reject_late_completion(SlAppRequestScope* request_scope,
                                                     SlAppRequestOutcome outcome, SlDiag* out_diag);
/*
 * Closes an already-terminal request scope. Callers must record the terminal outcome with
 * `sl_app_request_scope_complete` first unless the app lifecycle has already forced shutdown.
 */
SlStatus sl_app_request_scope_close(SlAppRequestScope* request_scope, SlDiag* out_diag);
bool sl_app_request_scope_is_active(const SlAppRequestScope* request_scope);
bool sl_app_request_scope_is_terminal(const SlAppRequestScope* request_scope);
SlStatus sl_app_request_scope_terminal_status(const SlAppRequestScope* request_scope);
SlDiagCode sl_app_request_scope_terminal_diag_code(const SlAppRequestScope* request_scope);
SlCancellationReason sl_app_request_scope_terminal_reason(const SlAppRequestScope* request_scope);
uint64_t sl_app_request_scope_app_id(const SlAppRequestScope* request_scope);
uint64_t sl_app_request_scope_request_id(const SlAppRequestScope* request_scope);
SlAppRequestScopeSnapshot sl_app_request_scope_snapshot(const SlAppRequestScope* request_scope);
SlStatus sl_app_request_scope_assert_no_leaks(const SlAppRequestScope* request_scope,
                                              SlDiag* out_diag);

/*
 * Runs one native request handler with a deterministic request cleanup scope.
 *
 * The request scope begins before `handler` is called and is closed after handler success or
 * failure. Cleanup callbacks run through SlScope's existing LIFO contract. Handler failure
 * is preserved unless cleanup itself fails, which is currently representable only as an
 * internal lifecycle error.
 */
SlStatus sl_app_request_scope_execute(SlScopeCleanup* storage, size_t cleanup_capacity,
                                      SlAppRequestScopeHandler handler, void* user,
                                      SlDiag* out_diag);
SlStatus sl_app_request_scope_execute_for_app(SlAppLifecycle* lifecycle, uint64_t request_id,
                                              SlScopeCleanup* storage, size_t cleanup_capacity,
                                              SlAppRequestScopeHandler handler, void* user,
                                              SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
