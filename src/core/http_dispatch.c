/*
 * src/core/http_dispatch.c
 *
 * Implements TASK 10.C's synthetic in-memory GET dispatch path: parsed HTTP request head,
 * manual route binding, numeric Sloppy Plan handler ID, and existing runtime-contract
 * engine call. It deliberately stops before real server concerns.
 *
 * Safety invariants:
 * - route bindings and parsed patterns are borrowed for the call only;
 * - route params and query params are materialized into the dispatch arena for one call;
 * - missing plan handlers fail before entering the engine boundary;
 * - no socket, libuv loop, response writer, request context, middleware, OS API, V8 type,
 *   or public TypeScript API is introduced here.
 *
 * Tests: tests/unit/core/test_http_dispatch.c and the V8-gated HTTP dispatch integration.
 */
#include "sloppy/http_dispatch.h"

#include "sloppy/http_context.h"
#include "sloppy/runtime_contract.h"

#include "sloppy/checked_math.h"

typedef struct SlHttpRouteTableEntry
{
    SlRoutePattern pattern;
    SlHttpRouteBinding binding;
    size_t source_order;
    bool has_params;
} SlHttpRouteTableEntry;

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

static bool sl_http_plan_route_is_get(const SlPlanRoute* route)
{
    return route != NULL && sl_str_equal(route->method, sl_str_from_cstr("GET"));
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

static SlStatus sl_http_route_table_duplicate_route(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_DUPLICATE_ROUTE,
        sl_http_dispatch_literal("duplicate HTTP route", sizeof("duplicate HTTP route") - 1U),
        sl_http_dispatch_literal("route method and pattern pairs must be unique before serving",
                                 sizeof("route method and pattern pairs must be unique before "
                                        "serving") -
                                     1U),
        SL_STATUS_INVALID_STATE);
}

static SlStatus sl_http_route_table_invalid_route(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_INVALID_ROUTE_PATTERN,
        sl_http_dispatch_literal("HTTP route table contains an invalid route",
                                 sizeof("HTTP route table contains an invalid route") - 1U),
        sl_http_dispatch_literal("plan routes must use the supported alpha route syntax",
                                 sizeof("plan routes must use the supported alpha route syntax") -
                                     1U),
        SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_http_route_table_missing_handler(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
        sl_http_dispatch_literal("HTTP route table references a missing plan handler",
                                 sizeof("HTTP route table references a missing plan handler") - 1U),
        sl_http_dispatch_literal("routes[].handlerId must reference handlers[].id",
                                 sizeof("routes[].handlerId must reference handlers[].id") - 1U),
        SL_STATUS_INVALID_STATE);
}

static SlStatus sl_http_dispatch_unsupported_body(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_UNSUPPORTED_BODY,
        sl_http_dispatch_literal("HTTP request bodies are not supported",
                                 sizeof("HTTP request bodies are not supported") - 1U),
        sl_http_dispatch_literal("omit request bodies until body parsing is implemented",
                                 sizeof("omit request bodies until body parsing is implemented") -
                                     1U),
        SL_STATUS_UNSUPPORTED);
}

static int sl_http_dispatch_ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static bool sl_http_dispatch_str_iequal(SlStr left, SlStr right)
{
    size_t index = 0U;

    if ((left.ptr == NULL && left.length != 0U) || (right.ptr == NULL && right.length != 0U) ||
        left.length != right.length)
    {
        return false;
    }

    for (index = 0U; index < left.length; index += 1U) {
        if (sl_http_dispatch_ascii_lower((unsigned char)left.ptr[index]) !=
            sl_http_dispatch_ascii_lower((unsigned char)right.ptr[index]))
        {
            return false;
        }
    }

    return true;
}

static bool sl_http_dispatch_content_length_has_body(SlStr value)
{
    size_t index = 0U;

    if (value.ptr == NULL && value.length != 0U) {
        return true;
    }

    while (index < value.length && (value.ptr[index] == ' ' || value.ptr[index] == '\t')) {
        index += 1U;
    }

    while (index < value.length) {
        if (value.ptr[index] >= '1' && value.ptr[index] <= '9') {
            return true;
        }
        if (value.ptr[index] != '0' && value.ptr[index] != ' ' && value.ptr[index] != '\t') {
            return true;
        }
        index += 1U;
    }

    return false;
}

static bool sl_http_dispatch_request_declares_body(const SlHttpRequestHead* request)
{
    size_t index = 0U;

    if (request == NULL || (request->header_count != 0U && request->headers == NULL)) {
        return false;
    }

    for (index = 0U; index < request->header_count; index += 1U) {
        const SlHttpHeader* header = &request->headers[index];
        if (sl_http_dispatch_str_iequal(header->name, sl_str_from_cstr("Content-Length")) &&
            sl_http_dispatch_content_length_has_body(header->value))
        {
            return true;
        }
        if (sl_http_dispatch_str_iequal(header->name, sl_str_from_cstr("Transfer-Encoding"))) {
            return true;
        }
    }

    return false;
}

static bool sl_http_route_entry_less(const SlHttpRouteTableEntry* left,
                                     const SlHttpRouteTableEntry* right)
{
    if (left->has_params != right->has_params) {
        return !left->has_params;
    }

    return left->source_order < right->source_order;
}

static void sl_http_route_table_sort(SlHttpRouteTableEntry* entries, size_t count)
{
    size_t outer = 0U;

    for (outer = 1U; outer < count; outer += 1U) {
        SlHttpRouteTableEntry item = entries[outer];
        size_t inner = outer;

        while (inner > 0U && sl_http_route_entry_less(&item, &entries[inner - 1U])) {
            entries[inner] = entries[inner - 1U];
            inner -= 1U;
        }

        entries[inner] = item;
    }
}

static SlStatus sl_http_route_table_alloc(SlArena* arena, size_t route_count,
                                          SlHttpRouteTableEntry** out_entries,
                                          SlHttpRouteBinding** out_bindings)
{
    void* entry_memory = NULL;
    void* binding_memory = NULL;
    size_t alloc_size = 0U;
    SlStatus status;

    if (arena == NULL || out_entries == NULL || out_bindings == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_entries = NULL;
    *out_bindings = NULL;
    if (route_count == 0U) {
        return sl_status_ok();
    }

    status = sl_checked_mul_size(route_count, sizeof(SlHttpRouteTableEntry), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, alloc_size, _Alignof(SlHttpRouteTableEntry), &entry_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_mul_size(route_count, sizeof(SlHttpRouteBinding), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, alloc_size, _Alignof(SlHttpRouteBinding), &binding_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_entries = (SlHttpRouteTableEntry*)entry_memory;
    *out_bindings = (SlHttpRouteBinding*)binding_memory;
    return sl_status_ok();
}

static SlStatus sl_http_route_table_fill_entries(SlArena* arena, const SlPlan* plan,
                                                 SlHttpRouteTableEntry* entries,
                                                 size_t* out_entry_count, SlDiag* out_diag)
{
    size_t index = 0U;
    size_t entry_count = 0U;

    for (index = 0U; index < plan->route_count; index += 1U) {
        const SlPlanRoute* route = &plan->routes[index];
        const SlPlanHandler* handler = NULL;
        SlStatus status;

        if (!sl_plan_route_method_supported(route->method)) {
            return sl_http_dispatch_unsupported_method(arena, out_diag);
        }
        if (!sl_http_plan_route_is_get(route)) {
            continue;
        }

        entries[entry_count] = (SlHttpRouteTableEntry){0};
        status = sl_plan_find_handler_by_id(plan, route->handler_id, &handler);
        if (!sl_status_is_ok(status)) {
            return sl_http_route_table_missing_handler(arena, out_diag);
        }

        status = sl_route_pattern_parse(arena, route->pattern, &entries[entry_count].pattern, NULL);
        if (!sl_status_is_ok(status)) {
            return sl_http_route_table_invalid_route(arena, out_diag);
        }

        entries[entry_count].binding.method = SL_HTTP_METHOD_GET;
        entries[entry_count].binding.pattern = &entries[entry_count].pattern;
        entries[entry_count].binding.handler_id = route->handler_id;
        entries[entry_count].source_order = index;
        entries[entry_count].has_params = entries[entry_count].binding.pattern->param_count != 0U;
        entry_count += 1U;
    }

    if (out_entry_count != NULL) {
        *out_entry_count = entry_count;
    }
    return sl_status_ok();
}

static SlStatus sl_http_route_table_count_runnable_routes(SlArena* arena, const SlPlan* plan,
                                                          size_t* out_route_count, SlDiag* out_diag)
{
    size_t index = 0U;
    size_t route_count = 0U;

    for (index = 0U; index < plan->route_count; index += 1U) {
        if (!sl_plan_route_method_supported(plan->routes[index].method)) {
            return sl_http_dispatch_unsupported_method(arena, out_diag);
        }
        if (sl_http_plan_route_is_get(&plan->routes[index])) {
            route_count += 1U;
        }
    }

    *out_route_count = route_count;
    return sl_status_ok();
}

SlStatus sl_http_route_table_build(SlArena* arena, const SlPlan* plan, SlHttpRouteTable* out_table,
                                   SlDiag* out_diag)
{
    SlArenaMark mark = {0};
    SlHttpRouteTableEntry* entries = NULL;
    SlHttpRouteBinding* bindings = NULL;
    size_t index = 0U;
    size_t runnable_route_count = 0U;
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (out_table == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_table = (SlHttpRouteTable){0};
    if (arena == NULL || plan == NULL || (plan->route_count != 0U && plan->routes == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_plan_has_duplicate_routes(plan)) {
        return sl_http_route_table_duplicate_route(arena, out_diag);
    }

    mark = sl_arena_mark(arena);
    status =
        sl_http_route_table_count_runnable_routes(arena, plan, &runnable_route_count, out_diag);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    status = sl_http_route_table_alloc(arena, runnable_route_count, &entries, &bindings);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    if (runnable_route_count != 0U && (entries == NULL || bindings == NULL)) {
        status = sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        goto failure;
    }
    if (runnable_route_count == 0U) {
        out_table->dispatch.routes = NULL;
        out_table->dispatch.route_count = 0U;
        out_table->route_count = 0U;
        return sl_status_ok();
    }
    status =
        sl_http_route_table_fill_entries(arena, plan, entries, &runnable_route_count, out_diag);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    sl_http_route_table_sort(entries, runnable_route_count);
    for (index = 0U; index < runnable_route_count; index += 1U) {
        entries[index].binding.pattern = &entries[index].pattern;
        bindings[index] = entries[index].binding;
    }

    out_table->dispatch.routes = bindings;
    out_table->dispatch.route_count = runnable_route_count;
    out_table->route_count = runnable_route_count;
    return sl_status_ok();

failure:
    (void)sl_arena_reset_to(arena, mark);
    *out_table = (SlHttpRouteTable){0};
    return status;
}

static SlStatus sl_http_dispatch_find_route(SlArena* arena,
                                            const SlHttpDispatchTable* dispatch_table,
                                            const SlHttpRequestHead* request,
                                            const SlHttpRouteBinding** out_binding,
                                            SlRouteMatch* out_match)
{
    size_t index = 0U;

    if (arena == NULL || dispatch_table == NULL || request == NULL || out_binding == NULL ||
        out_match == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_binding = NULL;
    *out_match = (SlRouteMatch){0};
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
            *out_match = match;
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
    SlRouteMatch route_match = {0};
    SlHttpQuery query = {0};
    SlHttpRequestContext request_context = {0};
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

    if (sl_http_dispatch_request_declares_body(request)) {
        return sl_http_dispatch_unsupported_body(arena, out_diag);
    }

    if (request->path.length == 0U || request->path.ptr == NULL || request->path.ptr[0] != '/') {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_find_route(arena, dispatch_table, request, &binding, &route_match);
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

    status = sl_http_query_parse(arena, request->raw_target, &query);
    if (!sl_status_is_ok(status)) {
        return sl_http_dispatch_write_diag(
            arena, out_diag, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_dispatch_literal("HTTP query string is malformed",
                                     sizeof("HTTP query string is malformed") - 1U),
            sl_http_dispatch_literal("Percent escapes in query strings must use two hex digits.",
                                     sizeof("Percent escapes in query strings must use two hex "
                                            "digits.") -
                                         1U),
            SL_STATUS_INVALID_ARGUMENT);
    }

    request_context.request = request;
    request_context.route_params = route_match.params;
    request_context.route_param_count = route_match.param_count;
    request_context.query_params = query.params;
    request_context.query_param_count = query.param_count;

    return sl_runtime_contract_call_handler_with_context(engine, arena, plan, binding->handler_id,
                                                         &request_context, out_result, out_diag);
}
