#ifndef SLOPPY_APP_HOST_H
#define SLOPPY_APP_HOST_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/plan.h"
#include "sloppy/scope.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>

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

typedef struct SlAppRequestScope
{
    SlScope cleanups;
    bool active;
} SlAppRequestScope;

typedef SlStatus (*SlAppRequestScopeHandler)(SlAppRequestScope* request_scope, void* user,
                                             SlDiag* out_diag);

SlStatus sl_app_request_scope_init(SlAppRequestScope* request_scope, SlScopeCleanup* storage,
                                   size_t cleanup_capacity);
SlStatus sl_app_request_scope_add_cleanup(SlAppRequestScope* request_scope, SlScopeCleanupFn fn,
                                          void* payload, void* user);
SlStatus sl_app_request_scope_close(SlAppRequestScope* request_scope, SlDiag* out_diag);
bool sl_app_request_scope_is_active(const SlAppRequestScope* request_scope);

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

#ifdef __cplusplus
}
#endif

#endif
