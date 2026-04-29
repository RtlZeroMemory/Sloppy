/*
 * src/core/http_dispatch.c
 *
 * Implements TASK 10.C's synthetic in-memory GET dispatch path: parsed HTTP request head,
 * manual route binding, numeric Sloppy Plan handler ID, and existing runtime-contract
 * engine call. It deliberately stops before real server concerns.
 *
 * Safety invariants:
 * - route bindings and parsed patterns are borrowed for the call only;
 * - route params are matched only to prove route-pattern integration and are not retained;
 * - missing plan handlers fail before entering the engine boundary;
 * - no socket, libuv loop, response writer, request context, middleware, OS API, V8 type,
 *   or public TypeScript API is introduced here.
 *
 * Tests: tests/unit/core/test_http_dispatch.c and the V8-gated HTTP dispatch integration.
 */
#include "sloppy/http_dispatch.h"

#include "sloppy/runtime_contract.h"

static SlStr sl_http_dispatch_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlStatus sl_http_dispatch_write_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                            SlStr message, SlStr hint, SlStatusCode status_code)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(status_code);
    }

    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_diag = (SlDiag){0};
    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_str_is_empty(hint)) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(status_code);
}

static SlStatus sl_http_dispatch_validate_table(const SlHttpDispatchTable* dispatch_table)
{
    if (dispatch_table == NULL ||
        (dispatch_table->route_count != 0U && dispatch_table->routes == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_http_dispatch_missing_route(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_ROUTE_NOT_FOUND,
        sl_http_dispatch_literal("no matching HTTP route was found",
                                 sizeof("no matching HTTP route was found") - 1U),
        sl_http_dispatch_literal("register a GET route binding for the parsed request path",
                                 sizeof("register a GET route binding for the parsed request "
                                        "path") -
                                     1U),
        SL_STATUS_OUT_OF_RANGE);
}

static SlStatus sl_http_dispatch_unsupported_method(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_UNSUPPORTED_METHOD,
        sl_http_dispatch_literal("HTTP dispatch supports GET only",
                                 sizeof("HTTP dispatch supports GET only") - 1U),
        sl_http_dispatch_literal("TASK 10.C rejects non-GET methods before route matching",
                                 sizeof("TASK 10.C rejects non-GET methods before route "
                                        "matching") -
                                     1U),
        SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_dispatch_missing_handler(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
        sl_http_dispatch_literal("matched route handler ID was not found in app plan",
                                 sizeof("matched route handler ID was not found in app plan") - 1U),
        sl_http_dispatch_literal("ensure the manual dispatch binding references a plan handler",
                                 sizeof("ensure the manual dispatch binding references a plan "
                                        "handler") -
                                     1U),
        SL_STATUS_OUT_OF_RANGE);
}

static SlStatus sl_http_dispatch_find_route(SlArena* arena,
                                            const SlHttpDispatchTable* dispatch_table,
                                            const SlHttpRequestHead* request,
                                            const SlHttpRouteBinding** out_binding)
{
    size_t index = 0U;

    if (arena == NULL || dispatch_table == NULL || request == NULL || out_binding == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_binding = NULL;
    for (index = 0U; index < dispatch_table->route_count; index += 1U) {
        const SlHttpRouteBinding* binding = &dispatch_table->routes[index];
        SlRouteMatch match = {0};
        SlStatus status;

        if (binding->method != SL_HTTP_METHOD_GET) {
            continue;
        }

        if (binding->pattern == NULL || !sl_handler_id_valid(binding->handler_id)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        status = sl_route_pattern_match(arena, binding->pattern, request->path, &match);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        if (match.matched) {
            *out_binding = binding;
            return sl_status_ok();
        }
    }

    return sl_status_ok();
}

SlStatus sl_http_dispatch_request_head(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                       const SlHttpDispatchTable* dispatch_table,
                                       const SlHttpRequestHead* request, SlEngineResult* out_result,
                                       SlDiag* out_diag)
{
    const SlHttpRouteBinding* binding = NULL;
    const SlPlanHandler* handler = NULL;
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};
    if (arena == NULL || engine == NULL || plan == NULL || request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_validate_table(dispatch_table);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (request->method != SL_HTTP_METHOD_GET) {
        return sl_http_dispatch_unsupported_method(arena, out_diag);
    }

    if (request->path.length == 0U || request->path.ptr == NULL || request->path.ptr[0] != '/') {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_find_route(arena, dispatch_table, request, &binding);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (binding == NULL) {
        return sl_http_dispatch_missing_route(arena, out_diag);
    }

    status = sl_plan_find_handler_by_id(plan, binding->handler_id, &handler);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        return sl_http_dispatch_missing_handler(arena, out_diag);
    }

    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_runtime_contract_call_handler(engine, arena, plan, binding->handler_id, out_result,
                                            out_diag);
}
