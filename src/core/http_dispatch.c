/*
 * src/core/http_dispatch.c
 *
 * Implements the bounded Plan-backed HTTP dispatch path: parsed HTTP request, route binding
 * method/path match, JSON/text body policy, numeric Sloppy Plan handler ID, and existing
 * runtime-contract engine call. It deliberately stops before production server concerns.
 *
 * Safety invariants:
 * - route bindings and parsed patterns are borrowed for the call only;
 * - route params, query params, and body policy are materialized for one call;
 * - missing plan handlers fail before entering the engine boundary;
 * - no socket, libuv loop, middleware, OS API, V8 type, or public TypeScript API is
 *   introduced here.
 *
 * Tests: tests/unit/core/test_http_dispatch.c and the V8-gated HTTP dispatch integration.
 */
#include "sloppy/http_dispatch.h"

#include "sloppy/breadcrumbs.h"
#include "sloppy/http_context.h"
#include "sloppy/http_profile.h"
#include "sloppy/ops_metrics.h"
#include "sloppy/platform_time.h"
#include "sloppy/request_validation.h"
#include "sloppy/runtime_contract.h"

#include "sloppy/builder.h"
#include "sloppy/container.h"

#include <yyjson.h>
#include <stdlib.h>
#include <string.h>

typedef struct SlHttpRouteTableEntry
{
    SlRoutePattern pattern;
    SlHttpRouteBinding binding;
    size_t source_order;
    bool has_params;
    size_t static_segment_count;
    size_t constrained_param_count;
} SlHttpRouteTableEntry;

typedef struct SlHttpDispatchContextSeed
{
    uint64_t request_id;
    uint64_t connection_id;
    SlStr scheme;
    size_t max_body_length;
    const SlCancellationToken* cancellation;
} SlHttpDispatchContextSeed;

typedef enum SlHttpDispatchJsonMode
{
    SL_HTTP_DISPATCH_JSON_NATIVE = 0,
    SL_HTTP_DISPATCH_JSON_GENERIC = 1,
    SL_HTTP_DISPATCH_JSON_VALIDATE = 2
} SlHttpDispatchJsonMode;

struct SlHttpRouteTrieRoot
{
    SlHttpMethod method;
    size_t root_index;
};

struct SlHttpRouteTrieNode
{
    const SlHttpRouteBinding* terminal;
    size_t first_edge;
};

struct SlHttpRouteTrieEdge
{
    SlRouteSegmentKind kind;
    SlRouteParamKind param_kind;
    SlStr text;
    size_t child_index;
    size_t next_edge;
};

#define SL_HTTP_ROUTE_TRIE_NONE ((size_t)-1)

static SlStr sl_http_dispatch_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static void sl_http_dispatch_record_breadcrumb(SlDiagSubsystem subsystem, SlBreadcrumbEvent event,
                                               SlStatusCode status, uint64_t request_id,
                                               uint64_t connection_id, uint64_t route_id,
                                               uint64_t handler_id, SlStr detail)
{
    if (!sl_breadcrumb_global_should_record(event, status)) {
        return;
    }
    sl_breadcrumb_global_record(subsystem, event, status, request_id, connection_id, route_id,
                                handler_id, detail);
    sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_BREADCRUMBS_RECORDED, 1U);
}

static SlHttpRouteDispatchMode sl_http_route_dispatch_mode_from_cstr(const char* value)
{
    if (value == NULL || value[0] == '\0' || strcmp(value, "compiled") == 0) {
        return SL_HTTP_ROUTE_DISPATCH_MODE_COMPILED;
    }
    if (strcmp(value, "classic") == 0) {
        return SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC;
    }
    if (strcmp(value, "validate") == 0) {
        return SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE;
    }
    return SL_HTTP_ROUTE_DISPATCH_MODE_COMPILED;
}

static SlHttpRouteDispatchMode sl_http_route_dispatch_mode_from_env(void)
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    return sl_http_route_dispatch_mode_from_cstr(getenv("SLOPPY_ROUTE_DISPATCH"));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

static SlStatus sl_http_dispatch_write_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                            SlStr message, SlStr hint, SlStatusCode status_code)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        sl_http_dispatch_record_breadcrumb(sl_diag_metadata_for_code(code).subsystem,
                                           SL_BREADCRUMB_EVENT_HTTP_REQUEST_END, status_code, 0U,
                                           0U, 0U, 0U, message);
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

    sl_http_dispatch_record_breadcrumb(sl_diag_metadata_for_code(code).subsystem,
                                       SL_BREADCRUMB_EVENT_HTTP_REQUEST_END, status_code, 0U, 0U,
                                       0U, 0U, message);
    return sl_status_from_code(status_code);
}

static bool sl_http_dispatch_power_of_two(size_t value);
static bool sl_http_dispatch_binding_valid(const SlHttpRouteBinding* binding);

#define SL_HTTP_DISPATCH_TABLE_METHOD_MASK 0x3fU
#define SL_HTTP_DISPATCH_TABLE_VALIDATED_FLAG (1U << 31U)

static SlStatus sl_http_dispatch_validate_table(const SlHttpDispatchTable* dispatch_table)
{
    size_t index = 0U;
    bool table_validated = false;

    if (dispatch_table == NULL ||
        (dispatch_table->route_count != 0U && dispatch_table->routes == NULL) ||
        (dispatch_table->exact_route_bucket_count != 0U &&
         dispatch_table->exact_route_buckets == NULL) ||
        (dispatch_table->param_route_count != 0U && dispatch_table->param_routes == NULL) ||
        (dispatch_table->param_route_bucket_count != 0U &&
         dispatch_table->param_route_buckets == NULL) ||
        (dispatch_table->param_route_bucket_slot_count != 0U &&
         dispatch_table->param_route_bucket_slots == NULL) ||
        (dispatch_table->param_route_bucket_slot_count != 0U &&
         !sl_http_dispatch_power_of_two(dispatch_table->param_route_bucket_slot_count)) ||
        (dispatch_table->param_route_trie_root_count != 0U &&
         dispatch_table->param_route_trie_roots == NULL) ||
        (dispatch_table->param_route_trie_node_count != 0U &&
         dispatch_table->param_route_trie_nodes == NULL) ||
        (dispatch_table->param_route_trie_edge_count != 0U &&
         dispatch_table->param_route_trie_edges == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if ((dispatch_table->runtime_reserved0 &
         ~(SL_HTTP_DISPATCH_TABLE_METHOD_MASK | SL_HTTP_DISPATCH_TABLE_VALIDATED_FLAG)) != 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (dispatch_table->dispatch_mode != SL_HTTP_ROUTE_DISPATCH_MODE_COMPILED &&
        dispatch_table->dispatch_mode != SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC &&
        dispatch_table->dispatch_mode != SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    table_validated =
        (dispatch_table->runtime_reserved0 & SL_HTTP_DISPATCH_TABLE_VALIDATED_FLAG) != 0U;

    if (!table_validated && dispatch_table->handler_cache_trusted) {
        if (dispatch_table->plan == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        for (index = 0U; index < dispatch_table->route_count; index += 1U) {
            const SlHttpRouteBinding* binding = &dispatch_table->routes[index];
            if (binding->handler == NULL || binding->handler->id != binding->handler_id) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
        }
    }

    if (!table_validated) {
        for (index = 0U; index < dispatch_table->param_route_bucket_count; index += 1U) {
            const SlHttpRouteCandidateBucket* bucket = &dispatch_table->param_route_buckets[index];
            if (bucket->route_count != 0U && bucket->routes == NULL) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
        }
    }

    return sl_status_ok();
}

static SlStatus sl_http_dispatch_missing_route(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_ROUTE_NOT_FOUND,
        sl_http_dispatch_literal("no matching HTTP route was found",
                                 sizeof("no matching HTTP route was found") - 1U),
        sl_http_dispatch_literal("register a route binding for the parsed request path",
                                 sizeof("register a route binding for the parsed request path") -
                                     1U),
        SL_STATUS_OUT_OF_RANGE);
}

static SlStatus sl_http_dispatch_method_not_allowed(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_METHOD_NOT_ALLOWED,
        sl_http_dispatch_literal("HTTP method is not allowed for this route",
                                 sizeof("HTTP method is not allowed for this route") - 1U),
        sl_http_dispatch_literal("register a route with matching method metadata before serving",
                                 sizeof("register a route with matching method metadata before "
                                        "serving") -
                                     1U),
        SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_dispatch_unsupported_method(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_UNSUPPORTED_METHOD,
        sl_http_dispatch_literal("HTTP method is not supported by the framework runtime",
                                 sizeof("HTTP method is not supported by the framework runtime") -
                                     1U),
        sl_http_dispatch_literal(
            "supported request methods are GET, HEAD, POST, PUT, PATCH, DELETE, and OPTIONS",
            sizeof("supported request methods are GET, HEAD, POST, PUT, PATCH, DELETE, and "
                   "OPTIONS") -
                1U),
        SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_dispatch_websocket_requires_upgrade(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_INVALID_HTTP_REQUEST,
        sl_http_dispatch_literal("WebSocket route requires an HTTP Upgrade request",
                                 sizeof("WebSocket route requires an HTTP Upgrade request") - 1U),
        sl_http_dispatch_literal("send Upgrade: websocket with the required WebSocket headers",
                                 sizeof("send Upgrade: websocket with the required WebSocket "
                                        "headers") -
                                     1U),
        SL_STATUS_INVALID_ARGUMENT);
}

static bool sl_http_plan_route_is_runnable(const SlPlanRoute* route)
{
    return route != NULL && sl_plan_route_method_runnable(route->method);
}

static bool sl_http_dispatch_request_method_runnable(SlHttpMethod method)
{
    return method == SL_HTTP_METHOD_HEAD || method == SL_HTTP_METHOD_OPTIONS ||
           sl_http_method_supported(method);
}

static bool sl_http_dispatch_binding_method_runnable(SlHttpMethod method)
{
    return method == SL_HTTP_METHOD_OPTIONS || sl_http_method_supported(method);
}

static SlHttpMethod sl_http_dispatch_route_match_method(SlHttpMethod method)
{
    return method == SL_HTTP_METHOD_HEAD ? SL_HTTP_METHOD_GET : method;
}

static SlStatus sl_http_dispatch_method_from_plan(SlStr method, SlHttpMethod* out_method)
{
    SlStatus status = sl_http_method_from_str(method, out_method);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http_dispatch_binding_method_runnable(*out_method)
               ? sl_status_ok()
               : sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

static unsigned int sl_http_dispatch_method_bit(SlHttpMethod method)
{
    if (method == SL_HTTP_METHOD_GET) {
        return 1U << 0U;
    }
    if (method == SL_HTTP_METHOD_POST) {
        return 1U << 1U;
    }
    if (method == SL_HTTP_METHOD_PUT) {
        return 1U << 2U;
    }
    if (method == SL_HTTP_METHOD_PATCH) {
        return 1U << 3U;
    }
    if (method == SL_HTTP_METHOD_DELETE) {
        return 1U << 4U;
    }
    if (method == SL_HTTP_METHOD_OPTIONS) {
        return 1U << 5U;
    }
    return 0U;
}

static bool sl_http_dispatch_plan_route_matches_binding(const SlPlanRoute* route,
                                                        const SlHttpRouteBinding* binding)
{
    SlHttpMethod method = SL_HTTP_METHOD_UNKNOWN;

    if (route == NULL || binding == NULL || binding->pattern == NULL ||
        route->handler_id != binding->handler_id)
    {
        return false;
    }
    if (!sl_status_is_ok(sl_http_dispatch_method_from_plan(route->method, &method)) ||
        method != binding->method)
    {
        return false;
    }
    return sl_str_equal(route->pattern, binding->pattern->source);
}

static const SlPlanRoute* sl_http_dispatch_find_validation_route(const SlPlan* plan,
                                                                 const SlHttpRouteBinding* binding)
{
    size_t index = 0U;

    if (plan == NULL || binding == NULL) {
        return NULL;
    }
    if (binding->route_index < plan->route_count &&
        sl_http_dispatch_plan_route_matches_binding(&plan->routes[binding->route_index], binding))
    {
        return &plan->routes[binding->route_index];
    }
    for (index = 0U; index < plan->route_count; index += 1U) {
        if (sl_http_dispatch_plan_route_matches_binding(&plan->routes[index], binding)) {
            return &plan->routes[index];
        }
    }
    return NULL;
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

static SlStatus sl_http_route_table_duplicate_handler(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_DUPLICATE_HANDLER_ID,
        sl_http_dispatch_literal("duplicate handler id metadata was found",
                                 sizeof("duplicate handler id metadata was found") - 1U),
        sl_http_dispatch_literal("deduplicate handler ids before building a route dispatch table",
                                 sizeof("deduplicate handler ids before building a route dispatch "
                                        "table") -
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
        sl_http_dispatch_literal("HTTP request body framing is not supported",
                                 sizeof("HTTP request body framing is not supported") - 1U),
        sl_http_dispatch_literal("send a bounded Content-Length body instead of transfer encoding",
                                 sizeof("send a bounded Content-Length body instead of transfer "
                                        "encoding") -
                                     1U),
        SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_dispatch_body_too_large(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_BODY_LIMIT,
        sl_http_dispatch_literal("HTTP request body is too large",
                                 sizeof("HTTP request body is too large") - 1U),
        sl_http_dispatch_literal("keep request bodies within the documented alpha limit",
                                 sizeof("keep request bodies within the documented alpha limit") -
                                     1U),
        SL_STATUS_CAPACITY_EXCEEDED);
}

static SlStatus sl_http_dispatch_unsupported_media_type(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE,
        sl_http_dispatch_literal("HTTP request body content type is not supported",
                                 sizeof("HTTP request body content type is not supported") - 1U),
        sl_http_dispatch_literal(
            "use application/json, text/plain, application/octet-stream, "
            "application/x-www-form-urlencoded, or multipart/form-data for "
            "bounded request bodies",
            sizeof("use application/json, text/plain, application/octet-stream, "
                   "application/x-www-form-urlencoded, or multipart/form-data "
                   "for bounded request bodies") -
                1U),
        SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_dispatch_malformed_json(SlArena* arena, SlDiag* out_diag)
{
    return sl_http_dispatch_write_diag(
        arena, out_diag, SL_DIAG_MALFORMED_JSON,
        sl_http_dispatch_literal("HTTP request JSON body is malformed",
                                 sizeof("HTTP request JSON body is malformed") - 1U),
        sl_http_dispatch_literal("send valid JSON or use text/plain for raw text bodies",
                                 sizeof("send valid JSON or use text/plain for raw text bodies") -
                                     1U),
        SL_STATUS_INVALID_ARGUMENT);
}

static SlStr sl_http_dispatch_trim_ascii_space(SlStr value)
{
    size_t begin = 0U;
    size_t end = value.length;

    if (value.ptr == NULL) {
        return sl_str_empty();
    }

    while (begin < end && (value.ptr[begin] == ' ' || value.ptr[begin] == '\t')) {
        begin += 1U;
    }
    while (end > begin && (value.ptr[end - 1U] == ' ' || value.ptr[end - 1U] == '\t')) {
        end -= 1U;
    }

    return sl_str_from_parts(value.ptr + begin, end - begin);
}

static SlStr sl_http_dispatch_media_type(SlStr content_type)
{
    size_t index = 0U;

    if (content_type.ptr == NULL) {
        return sl_str_empty();
    }

    while (index < content_type.length && content_type.ptr[index] != ';') {
        index += 1U;
    }

    return sl_http_dispatch_trim_ascii_space(sl_str_from_parts(content_type.ptr, index));
}

static bool sl_http_dispatch_request_declares_transfer_encoding(const SlHttpRequestHead* request)
{
    size_t index = 0U;

    if (request == NULL || (request->header_count != 0U && request->headers == NULL)) {
        return false;
    }

    for (index = 0U; index < request->header_count; index += 1U) {
        const SlHttpHeader* header = &request->headers[index];
        if (sl_str_equal_ci_ascii(header->name, SL_STR_LITERAL("Transfer-Encoding"))) {
            return true;
        }
    }

    return false;
}

static bool sl_http_dispatch_find_header(const SlHttpRequestHead* request, SlStr name,
                                         SlStr* out_value)
{
    size_t index = 0U;

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (request == NULL || out_value == NULL ||
        (request->header_count != 0U && request->headers == NULL))
    {
        return false;
    }

    for (index = 0U; index < request->header_count; index += 1U) {
        const SlHttpHeader* header = &request->headers[index];
        if (sl_str_equal_ci_ascii(header->name, name)) {
            *out_value = header->value;
            return true;
        }
    }

    return false;
}

static bool sl_http_dispatch_request_transfer_encoding_chunked(const SlHttpRequestHead* request)
{
    SlStr value = {0};

    if (!sl_http_dispatch_find_header(request, SL_STR_LITERAL("Transfer-Encoding"), &value)) {
        return false;
    }
    return sl_str_equal_ci_ascii(sl_http_dispatch_trim_ascii_space(value),
                                 SL_STR_LITERAL("chunked"));
}

static SlStr sl_http_dispatch_extract_query_string(SlStr raw_target)
{
    size_t index = 0U;

    if (raw_target.ptr == NULL) {
        return sl_str_empty();
    }

    for (index = 0U; index < raw_target.length; index += 1U) {
        if (raw_target.ptr[index] == '?') {
            return sl_str_from_parts(raw_target.ptr + index + 1U, raw_target.length - index - 1U);
        }
    }

    return sl_str_empty();
}

static bool sl_http_dispatch_parse_size_decimal(SlStr value, size_t* out)
{
    size_t index = 0U;
    size_t parsed = 0U;

    if (out == NULL || value.length == 0U || (value.ptr == NULL && value.length != 0U)) {
        return false;
    }
    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (ch < '0' || ch > '9') {
            return false;
        }
        if (parsed > (SIZE_MAX - (size_t)(ch - '0')) / 10U) {
            return false;
        }
        parsed = (parsed * 10U) + (size_t)(ch - '0');
    }

    *out = parsed;
    return true;
}

static SlStr sl_http_dispatch_protocol_name(const SlHttpRequestHead* request)
{
    if (request == NULL || request->version_major != 1U) {
        return sl_str_empty();
    }
    if (request->version_minor == 0U) {
        return SL_STR_LITERAL("HTTP/1.0");
    }
    if (request->version_minor == 1U) {
        return SL_STR_LITERAL("HTTP/1.1");
    }
    return sl_str_empty();
}

static void sl_http_dispatch_apply_metadata(const SlHttpRequestHead* request,
                                            const SlHttpDispatchContextSeed* seed,
                                            SlHttpRequestContext* request_context)
{
    SlStr content_type = sl_str_empty();
    SlStr content_length = sl_str_empty();
    size_t parsed_content_length = 0U;

    if (request == NULL || request_context == NULL) {
        return;
    }

    request_context->scheme =
        seed == NULL || seed->scheme.ptr == NULL || sl_str_is_empty(seed->scheme)
            ? SL_STR_LITERAL("http")
            : seed->scheme;
    request_context->protocol = sl_http_dispatch_protocol_name(request);
    request_context->query_string = sl_http_dispatch_extract_query_string(request->raw_target);
    if (seed != NULL) {
        request_context->request_id = seed->request_id;
        request_context->connection_id = seed->connection_id;
        request_context->cancellation = seed->cancellation;
    }
    if (sl_http_dispatch_find_header(request, SL_STR_LITERAL("Content-Type"), &content_type)) {
        request_context->content_type = sl_http_dispatch_trim_ascii_space(content_type);
    }
    if (sl_http_dispatch_find_header(request, SL_STR_LITERAL("Content-Length"), &content_length) &&
        sl_http_dispatch_parse_size_decimal(sl_http_dispatch_trim_ascii_space(content_length),
                                            &parsed_content_length))
    {
        request_context->has_content_length = true;
        request_context->content_length = (uint64_t)parsed_content_length;
    }
}

static bool sl_http_dispatch_media_type_json(SlStr media_type)
{
    return sl_str_equal_ci_ascii(media_type, SL_STR_LITERAL("application/json")) ||
           (sl_str_starts_with_ci_ascii(media_type, SL_STR_LITERAL("application/")) &&
            sl_str_ends_with_ci_ascii(media_type, SL_STR_LITERAL("+json")));
}

static SlStatus sl_http_dispatch_validate_json_body(SlArena* arena, SlBytes body, SlDiag* out_diag)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;

    if (body.length != 0U && body.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    doc = yyjson_read_opts((char*)body.ptr, body.length, 0U, NULL, &error);
    if (doc == NULL) {
        return sl_http_dispatch_malformed_json(arena, out_diag);
    }

    yyjson_doc_free(doc);
    return sl_status_ok();
}

static SlStatus sl_http_dispatch_apply_body_policy(SlArena* arena, const SlHttpRequestHead* request,
                                                   size_t max_body_length, bool json_body_expected,
                                                   bool native_json_validation,
                                                   SlHttpRequestBodyKind* out_body_kind,
                                                   SlDiag* out_diag)
{
    SlStr content_length = {0};
    SlStr content_type = {0};
    SlStr media_type = {0};

    if (out_body_kind == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_body_kind = SL_HTTP_REQUEST_BODY_NONE;
    if (request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (max_body_length == 0U) {
        max_body_length = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
    }

    if (sl_http_dispatch_request_declares_transfer_encoding(request) &&
        !sl_http_dispatch_request_transfer_encoding_chunked(request))
    {
        return sl_http_dispatch_unsupported_body(arena, out_diag);
    }

    if (request->body.length == 0U) {
        if (sl_http_dispatch_find_header(request, SL_STR_LITERAL("Content-Type"), &content_type)) {
            media_type = sl_http_dispatch_media_type(content_type);
            if (json_body_expected && !sl_http_dispatch_media_type_json(media_type)) {
                return sl_http_dispatch_unsupported_media_type(arena, out_diag);
            }
            if (sl_http_dispatch_media_type_json(media_type)) {
                *out_body_kind = SL_HTTP_REQUEST_BODY_JSON;
            }
            else if (sl_str_equal_ci_ascii(media_type,
                                           SL_STR_LITERAL("application/x-www-form-urlencoded")))
            {
                *out_body_kind = SL_HTTP_REQUEST_BODY_FORM;
            }
            else if (sl_str_equal_ci_ascii(media_type, SL_STR_LITERAL("multipart/form-data"))) {
                *out_body_kind = SL_HTTP_REQUEST_BODY_MULTIPART;
            }
        }
        return sl_status_ok();
    }
    if (request->body.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_http_dispatch_request_transfer_encoding_chunked(request) &&
        (!sl_http_dispatch_find_header(request, SL_STR_LITERAL("Content-Length"),
                                       &content_length) ||
         sl_str_is_empty(sl_http_dispatch_trim_ascii_space(content_length))))
    {
        return sl_http_dispatch_unsupported_body(arena, out_diag);
    }
    if (request->body.length > max_body_length) {
        return sl_http_dispatch_body_too_large(arena, out_diag);
    }

    if (!sl_http_dispatch_find_header(request, SL_STR_LITERAL("Content-Type"), &content_type)) {
        return sl_http_dispatch_unsupported_media_type(arena, out_diag);
    }

    media_type = sl_http_dispatch_media_type(content_type);
    if (json_body_expected && !sl_http_dispatch_media_type_json(media_type)) {
        return sl_http_dispatch_unsupported_media_type(arena, out_diag);
    }
    if (sl_http_dispatch_media_type_json(media_type)) {
        if (!native_json_validation) {
            SlStatus status = sl_http_dispatch_validate_json_body(arena, request->body, out_diag);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        *out_body_kind = SL_HTTP_REQUEST_BODY_JSON;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, SL_STR_LITERAL("text/plain"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_TEXT;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, SL_STR_LITERAL("application/octet-stream"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_BYTES;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, SL_STR_LITERAL("application/x-www-form-urlencoded"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_FORM;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, SL_STR_LITERAL("multipart/form-data"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_MULTIPART;
        return sl_status_ok();
    }

    return sl_http_dispatch_unsupported_media_type(arena, out_diag);
}

static bool sl_http_route_entry_less(const SlHttpRouteTableEntry* left,
                                     const SlHttpRouteTableEntry* right)
{
    size_t index = 0U;
    size_t shared_segments = left->pattern.segment_count < right->pattern.segment_count
                                 ? left->pattern.segment_count
                                 : right->pattern.segment_count;

    for (index = 0U; index < shared_segments; index += 1U) {
        const SlRouteSegment* left_segment = &left->pattern.segments[index];
        const SlRouteSegment* right_segment = &right->pattern.segments[index];
        bool left_param = left_segment->kind == SL_ROUTE_SEGMENT_PARAM;
        bool right_param = right_segment->kind == SL_ROUTE_SEGMENT_PARAM;
        bool left_constrained = left_param && left_segment->param_kind != SL_ROUTE_PARAM_STRING;
        bool right_constrained = right_param && right_segment->param_kind != SL_ROUTE_PARAM_STRING;

        if (left_param != right_param) {
            return !left_param;
        }
        if (left_param && left_constrained != right_constrained) {
            return left_constrained;
        }
        if (!left_param && !right_param && !sl_str_equal(left_segment->text, right_segment->text)) {
            return sl_str_compare(left_segment->text, right_segment->text) < 0;
        }
    }

    if (left->pattern.segment_count != right->pattern.segment_count) {
        return left->pattern.segment_count > right->pattern.segment_count;
    }

    return left->source_order < right->source_order;
}

static int sl_http_route_table_entry_compare(const void* left, const void* right)
{
    const SlHttpRouteTableEntry* left_entry = (const SlHttpRouteTableEntry*)left;
    const SlHttpRouteTableEntry* right_entry = (const SlHttpRouteTableEntry*)right;

    if (sl_http_route_entry_less(left_entry, right_entry)) {
        return -1;
    }
    if (sl_http_route_entry_less(right_entry, left_entry)) {
        return 1;
    }
    return 0;
}

static size_t sl_http_route_table_partition_exact_first(SlHttpRouteTableEntry* entries,
                                                        size_t count)
{
    size_t index = 0U;
    size_t exact_count = 0U;

    if (entries == NULL) {
        return 0U;
    }
    for (index = 0U; index < count; index += 1U) {
        if (entries[index].has_params) {
            continue;
        }
        if (index != exact_count) {
            SlHttpRouteTableEntry item = entries[index];
            entries[index] = entries[exact_count];
            entries[exact_count] = item;
        }
        exact_count += 1U;
    }
    return exact_count;
}

static void sl_http_route_table_sort_param_entries(SlHttpRouteTableEntry* entries, size_t count)
{
    if (entries == NULL || count < 2U) {
        return;
    }
    qsort(entries, count, sizeof(SlHttpRouteTableEntry), sl_http_route_table_entry_compare);
}

static SlStatus sl_http_route_table_alloc(SlArena* arena, size_t route_count,
                                          SlHttpRouteTableEntry** out_entries,
                                          SlHttpRouteBinding** out_bindings)
{
    SlSlice entry_slice = {0};
    SlSlice binding_slice = {0};
    SlStatus status;

    if (arena == NULL || out_entries == NULL || out_bindings == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_entries = NULL;
    *out_bindings = NULL;
    if (route_count == 0U) {
        return sl_status_ok();
    }

    status = sl_arena_array_alloc(arena, route_count, sizeof(SlHttpRouteTableEntry),
                                  _Alignof(SlHttpRouteTableEntry), &entry_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_array_alloc(arena, route_count, sizeof(SlHttpRouteBinding),
                                  _Alignof(SlHttpRouteBinding), &binding_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_entries = (SlHttpRouteTableEntry*)entry_slice.ptr;
    *out_bindings = (SlHttpRouteBinding*)binding_slice.ptr;
    return sl_status_ok();
}

static bool sl_http_route_binding_exact_static(const SlHttpRouteBinding* binding)
{
    return binding != NULL && binding->pattern != NULL && binding->pattern->param_count == 0U;
}

static size_t sl_http_dispatch_hash_step(size_t hash, unsigned char byte)
{
    return ((hash ^ (size_t)byte) * (size_t)16777619U);
}

static size_t sl_http_dispatch_exact_hash(SlHttpMethod method, SlStr path)
{
    size_t index = 0U;
    size_t hash = (size_t)2166136261U;

    hash = sl_http_dispatch_hash_step(hash, (unsigned char)method);
    for (index = 0U; index < path.length; index += 1U) {
        hash = sl_http_dispatch_hash_step(hash, (unsigned char)path.ptr[index]);
    }
    return hash;
}

static size_t sl_http_dispatch_param_bucket_hash(SlHttpMethod method, SlStr first_static_segment)
{
    return sl_http_dispatch_exact_hash(method, first_static_segment);
}

static bool sl_http_dispatch_power_of_two(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool sl_http_dispatch_next_bucket_count(size_t value, size_t* out)
{
    size_t target = value == 0U ? 0U : value * 2U;
    size_t bucket_count = 1U;

    if (out == NULL || (value != 0U && target / 2U != value)) {
        return false;
    }
    while (bucket_count < target) {
        if (bucket_count > (SIZE_MAX / 2U)) {
            return false;
        }
        bucket_count *= 2U;
    }
    *out = bucket_count;
    return true;
}

static size_t sl_http_route_table_plan_route_hash(const SlPlanRoute* route)
{
    size_t hash = (size_t)2166136261U;
    size_t index = 0U;

    if (route == NULL) {
        return hash;
    }
    for (index = 0U; index < route->method.length; index += 1U) {
        hash = sl_http_dispatch_hash_step(hash, (unsigned char)route->method.ptr[index]);
    }
    hash = sl_http_dispatch_hash_step(hash, 0xffU);
    for (index = 0U; index < route->pattern.length; index += 1U) {
        hash = sl_http_dispatch_hash_step(hash, (unsigned char)route->pattern.ptr[index]);
    }
    return hash;
}

static SlStatus sl_http_route_table_has_duplicate_plan_routes(SlArena* arena, const SlPlan* plan,
                                                              bool* out_has_duplicate)
{
    SlSlice slot_slice = {0};
    const SlPlanRoute** slots = NULL;
    size_t slot_count = 0U;
    size_t route_index = 0U;
    SlStatus status;

    if (out_has_duplicate != NULL) {
        *out_has_duplicate = false;
    }
    if (arena == NULL || plan == NULL || out_has_duplicate == NULL ||
        (plan->route_count != 0U && plan->routes == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (plan->route_count < 2U) {
        return sl_status_ok();
    }
    if (!sl_http_dispatch_next_bucket_count(plan->route_count, &slot_count)) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    status = sl_arena_array_alloc(arena, slot_count, sizeof(SlPlanRoute*), _Alignof(SlPlanRoute*),
                                  &slot_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    slots = (const SlPlanRoute**)slot_slice.ptr;
    for (route_index = 0U; route_index < slot_count; route_index += 1U) {
        slots[route_index] = NULL;
    }
    for (route_index = 0U; route_index < plan->route_count; route_index += 1U) {
        const SlPlanRoute* route = &plan->routes[route_index];
        size_t hash = sl_http_route_table_plan_route_hash(route);
        size_t mask = slot_count - 1U;
        size_t probe = 0U;

        for (probe = 0U; probe < slot_count; probe += 1U) {
            size_t slot_index = (hash + probe) & mask;
            const SlPlanRoute* existing = slots[slot_index];
            if (existing == NULL) {
                slots[slot_index] = route;
                break;
            }
            if (sl_str_equal(existing->method, route->method) &&
                sl_str_equal(existing->pattern, route->pattern))
            {
                *out_has_duplicate = true;
                return sl_status_ok();
            }
        }
        if (probe == slot_count) {
            return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http_route_table_insert_exact(SlHttpRouteBinding** buckets, size_t bucket_count,
                                                 SlHttpRouteBinding* binding)
{
    size_t mask = bucket_count - 1U;
    size_t hash;
    size_t probe = 0U;

    if (buckets == NULL || bucket_count == 0U || binding == NULL || binding->pattern == NULL ||
        !sl_http_dispatch_power_of_two(bucket_count))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    hash = sl_http_dispatch_exact_hash(binding->method, binding->pattern->source);
    for (probe = 0U; probe < bucket_count; probe += 1U) {
        size_t bucket_index = (hash + probe) & mask;
        if (buckets[bucket_index] == NULL) {
            buckets[bucket_index] = binding;
            return sl_status_ok();
        }
    }

    return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
}

static SlStatus sl_http_route_table_insert_param_bucket(SlHttpRouteCandidateBucket** slots,
                                                        size_t slot_count,
                                                        SlHttpRouteCandidateBucket* bucket)
{
    size_t mask = slot_count - 1U;
    size_t hash;
    size_t probe = 0U;

    if (slots == NULL || slot_count == 0U || bucket == NULL ||
        !sl_http_dispatch_power_of_two(slot_count))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    hash = sl_http_dispatch_param_bucket_hash(bucket->method, bucket->first_static_segment);
    for (probe = 0U; probe < slot_count; probe += 1U) {
        size_t bucket_index = (hash + probe) & mask;
        if (slots[bucket_index] == NULL) {
            slots[bucket_index] = bucket;
            return sl_status_ok();
        }
    }

    return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
}

static SlStr sl_http_route_binding_first_static_segment(const SlHttpRouteBinding* binding)
{
    const SlRoutePattern* pattern = binding == NULL ? NULL : binding->pattern;

    if (pattern == NULL || pattern->segment_count == 0U || pattern->segments == NULL ||
        pattern->segments[0].kind != SL_ROUTE_SEGMENT_STATIC)
    {
        return sl_str_empty();
    }

    return pattern->segments[0].text;
}

static SlHttpRouteCandidateBucket*
sl_http_route_table_find_param_bucket(SlHttpRouteCandidateBucket* buckets, size_t bucket_count,
                                      SlHttpMethod method, SlStr first_static_segment)
{
    size_t index = 0U;

    if (buckets == NULL) {
        return NULL;
    }

    for (index = 0U; index < bucket_count; index += 1U) {
        if (buckets[index].method == method &&
            sl_str_equal(buckets[index].first_static_segment, first_static_segment))
        {
            return &buckets[index];
        }
    }

    return NULL;
}

static SlStatus sl_http_route_table_build_param_index(SlArena* arena,
                                                      const SlHttpRouteBinding* param_routes,
                                                      size_t param_route_count,
                                                      SlHttpDispatchTable* dispatch)
{
    SlSlice bucket_slice = {0};
    SlSlice slot_slice = {0};
    SlHttpRouteCandidateBucket* buckets = NULL;
    SlHttpRouteCandidateBucket** bucket_slots = NULL;
    size_t bucket_count = 0U;
    size_t bucket_slot_count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || dispatch == NULL || (param_route_count != 0U && param_routes == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (param_route_count == 0U) {
        return sl_status_ok();
    }

    status = sl_arena_array_alloc(arena, param_route_count, sizeof(SlHttpRouteCandidateBucket),
                                  _Alignof(SlHttpRouteCandidateBucket), &bucket_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    buckets = (SlHttpRouteCandidateBucket*)bucket_slice.ptr;
    for (index = 0U; index < param_route_count; index += 1U) {
        buckets[index] = (SlHttpRouteCandidateBucket){0};
    }

    for (index = 0U; index < param_route_count; index += 1U) {
        const SlHttpRouteBinding* binding = &param_routes[index];
        SlStr first_static_segment = sl_http_route_binding_first_static_segment(binding);
        SlHttpRouteCandidateBucket* bucket = sl_http_route_table_find_param_bucket(
            buckets, bucket_count, binding->method, first_static_segment);
        if (bucket == NULL) {
            bucket = &buckets[bucket_count];
            bucket_count += 1U;
            bucket->method = binding->method;
            bucket->first_static_segment = first_static_segment;
            bucket->routes = NULL;
            bucket->route_count = 0U;
        }
        bucket->route_count += 1U;
    }

    for (index = 0U; index < bucket_count; index += 1U) {
        SlSlice route_slice = {0};
        status =
            sl_arena_array_alloc(arena, buckets[index].route_count, sizeof(SlHttpRouteBinding*),
                                 _Alignof(SlHttpRouteBinding*), &route_slice);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        buckets[index].routes = (const SlHttpRouteBinding**)route_slice.ptr;
        buckets[index].route_count = 0U;
    }

    for (index = 0U; index < param_route_count; index += 1U) {
        const SlHttpRouteBinding* binding = &param_routes[index];
        SlStr first_static_segment = sl_http_route_binding_first_static_segment(binding);
        SlHttpRouteCandidateBucket* bucket = sl_http_route_table_find_param_bucket(
            buckets, bucket_count, binding->method, first_static_segment);
        if (bucket == NULL || bucket->routes == NULL) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        bucket->routes[bucket->route_count] = binding;
        bucket->route_count += 1U;
    }

    if (!sl_http_dispatch_next_bucket_count(bucket_count, &bucket_slot_count)) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    status = sl_arena_array_alloc(arena, bucket_slot_count, sizeof(SlHttpRouteCandidateBucket*),
                                  _Alignof(SlHttpRouteCandidateBucket*), &slot_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    bucket_slots = (SlHttpRouteCandidateBucket**)slot_slice.ptr;
    for (index = 0U; index < bucket_slot_count; index += 1U) {
        bucket_slots[index] = NULL;
    }
    for (index = 0U; index < bucket_count; index += 1U) {
        status = sl_http_route_table_insert_param_bucket(bucket_slots, bucket_slot_count,
                                                         &buckets[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    dispatch->param_route_buckets = buckets;
    dispatch->param_route_bucket_count = bucket_count;
    dispatch->param_route_bucket_slots = (const SlHttpRouteCandidateBucket**)bucket_slots;
    dispatch->param_route_bucket_slot_count = bucket_slot_count;
    return sl_status_ok();
}

static size_t
sl_http_route_table_param_trie_segment_capacity(const SlHttpRouteBinding* param_routes,
                                                size_t param_route_count)
{
    size_t index = 0U;
    size_t capacity = 0U;

    if (param_routes == NULL) {
        return 0U;
    }

    for (index = 0U; index < param_route_count; index += 1U) {
        if (param_routes[index].pattern != NULL) {
            capacity += param_routes[index].pattern->segment_count;
        }
    }
    return capacity;
}

static SlHttpRouteTrieRoot* sl_http_route_table_find_param_trie_root(SlHttpRouteTrieRoot* roots,
                                                                     size_t root_count,
                                                                     SlHttpMethod method)
{
    size_t index = 0U;

    if (roots == NULL) {
        return NULL;
    }

    for (index = 0U; index < root_count; index += 1U) {
        if (roots[index].method == method) {
            return &roots[index];
        }
    }
    return NULL;
}

static SlStatus sl_http_route_table_append_trie_edge(SlHttpRouteTrieNode* nodes,
                                                     size_t* node_last_edges,
                                                     SlHttpRouteTrieEdge* edges,
                                                     size_t parent_index, size_t edge_index)
{
    if (nodes == NULL || node_last_edges == NULL || edges == NULL ||
        parent_index == SL_HTTP_ROUTE_TRIE_NONE)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (nodes[parent_index].first_edge == SL_HTTP_ROUTE_TRIE_NONE) {
        nodes[parent_index].first_edge = edge_index;
        node_last_edges[parent_index] = edge_index;
        return sl_status_ok();
    }

    if (node_last_edges[parent_index] == SL_HTTP_ROUTE_TRIE_NONE) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    edges[node_last_edges[parent_index]].next_edge = edge_index;
    node_last_edges[parent_index] = edge_index;
    return sl_status_ok();
}

static bool sl_http_route_table_trie_edge_matches_segment(const SlHttpRouteTrieEdge* edge,
                                                          const SlRouteSegment* segment)
{
    if (edge == NULL || segment == NULL || edge->kind != segment->kind) {
        return false;
    }
    if (segment->kind == SL_ROUTE_SEGMENT_STATIC) {
        return sl_str_equal(edge->text, segment->text);
    }
    if (segment->kind == SL_ROUTE_SEGMENT_PARAM) {
        return edge->param_kind == segment->param_kind;
    }
    return false;
}

static SlHttpRouteTrieEdge* sl_http_route_table_find_trie_edge(SlHttpRouteTrieNode* nodes,
                                                               SlHttpRouteTrieEdge* edges,
                                                               size_t node_index,
                                                               const SlRouteSegment* segment)
{
    size_t edge_index = SL_HTTP_ROUTE_TRIE_NONE;

    if (nodes == NULL || edges == NULL || segment == NULL || node_index == SL_HTTP_ROUTE_TRIE_NONE)
    {
        return NULL;
    }

    edge_index = nodes[node_index].first_edge;
    while (edge_index != SL_HTTP_ROUTE_TRIE_NONE) {
        SlHttpRouteTrieEdge* edge = &edges[edge_index];
        if (sl_http_route_table_trie_edge_matches_segment(edge, segment)) {
            return edge;
        }
        edge_index = edge->next_edge;
    }
    return NULL;
}

static SlStatus sl_http_route_table_build_param_trie(SlArena* arena,
                                                     const SlHttpRouteBinding* param_routes,
                                                     size_t param_route_count,
                                                     SlHttpDispatchTable* dispatch)
{
    SlSlice root_slice = {0};
    SlSlice node_slice = {0};
    SlSlice node_last_edge_slice = {0};
    SlSlice edge_slice = {0};
    SlHttpRouteTrieRoot* roots = NULL;
    SlHttpRouteTrieNode* nodes = NULL;
    size_t* node_last_edges = NULL;
    SlHttpRouteTrieEdge* edges = NULL;
    size_t segment_capacity = 0U;
    size_t root_count = 0U;
    size_t node_count = 0U;
    size_t edge_count = 0U;
    size_t route_index = 0U;
    SlStatus status;

    if (arena == NULL || dispatch == NULL || (param_route_count != 0U && param_routes == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (param_route_count == 0U) {
        return sl_status_ok();
    }

    segment_capacity =
        sl_http_route_table_param_trie_segment_capacity(param_routes, param_route_count);
    if (segment_capacity > SIZE_MAX - param_route_count) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_arena_array_alloc(arena, param_route_count, sizeof(SlHttpRouteTrieRoot),
                                  _Alignof(SlHttpRouteTrieRoot), &root_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_array_alloc(arena, segment_capacity + param_route_count,
                                  sizeof(SlHttpRouteTrieNode), _Alignof(SlHttpRouteTrieNode),
                                  &node_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_array_alloc(arena, segment_capacity + param_route_count, sizeof(size_t),
                                  _Alignof(size_t), &node_last_edge_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_array_alloc(arena, segment_capacity, sizeof(SlHttpRouteTrieEdge),
                                  _Alignof(SlHttpRouteTrieEdge), &edge_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    roots = (SlHttpRouteTrieRoot*)root_slice.ptr;
    nodes = (SlHttpRouteTrieNode*)node_slice.ptr;
    node_last_edges = (size_t*)node_last_edge_slice.ptr;
    edges = (SlHttpRouteTrieEdge*)edge_slice.ptr;
    for (route_index = 0U; route_index < param_route_count; route_index += 1U) {
        roots[route_index] = (SlHttpRouteTrieRoot){0};
    }
    for (route_index = 0U; route_index < segment_capacity + param_route_count; route_index += 1U) {
        nodes[route_index].terminal = NULL;
        nodes[route_index].first_edge = SL_HTTP_ROUTE_TRIE_NONE;
        node_last_edges[route_index] = SL_HTTP_ROUTE_TRIE_NONE;
    }
    for (route_index = 0U; route_index < segment_capacity; route_index += 1U) {
        edges[route_index] = (SlHttpRouteTrieEdge){0};
        edges[route_index].next_edge = SL_HTTP_ROUTE_TRIE_NONE;
        edges[route_index].child_index = SL_HTTP_ROUTE_TRIE_NONE;
    }

    for (route_index = 0U; route_index < param_route_count; route_index += 1U) {
        const SlHttpRouteBinding* binding = &param_routes[route_index];
        const SlRoutePattern* pattern = binding->pattern;
        SlHttpRouteTrieRoot* root =
            sl_http_route_table_find_param_trie_root(roots, root_count, binding->method);
        size_t node_index = SL_HTTP_ROUTE_TRIE_NONE;
        size_t segment_index = 0U;

        if (!sl_http_dispatch_binding_valid(binding) || pattern == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (root == NULL) {
            if (root_count >= param_route_count ||
                node_count >= segment_capacity + param_route_count)
            {
                return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
            }
            nodes[node_count].terminal = NULL;
            nodes[node_count].first_edge = SL_HTTP_ROUTE_TRIE_NONE;
            node_last_edges[node_count] = SL_HTTP_ROUTE_TRIE_NONE;
            roots[root_count].method = binding->method;
            roots[root_count].root_index = node_count;
            root = &roots[root_count];
            root_count += 1U;
            node_count += 1U;
        }
        node_index = root->root_index;

        for (segment_index = 0U; segment_index < pattern->segment_count; segment_index += 1U) {
            const SlRouteSegment* segment = &pattern->segments[segment_index];
            SlHttpRouteTrieEdge* edge =
                sl_http_route_table_find_trie_edge(nodes, edges, node_index, segment);
            if (edge == NULL) {
                size_t child_index = node_count;
                if (edge_count >= segment_capacity ||
                    node_count >= segment_capacity + param_route_count)
                {
                    return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
                }
                nodes[child_index].terminal = NULL;
                nodes[child_index].first_edge = SL_HTTP_ROUTE_TRIE_NONE;
                node_last_edges[child_index] = SL_HTTP_ROUTE_TRIE_NONE;
                edges[edge_count].kind = segment->kind;
                edges[edge_count].param_kind = segment->param_kind;
                edges[edge_count].text = segment->text;
                edges[edge_count].child_index = child_index;
                edges[edge_count].next_edge = SL_HTTP_ROUTE_TRIE_NONE;
                status = sl_http_route_table_append_trie_edge(nodes, node_last_edges, edges,
                                                              node_index, edge_count);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
                edge = &edges[edge_count];
                edge_count += 1U;
                node_count += 1U;
            }
            node_index = edge->child_index;
        }

        if (node_index == SL_HTTP_ROUTE_TRIE_NONE || node_index >= node_count) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        if (nodes[node_index].terminal == NULL) {
            nodes[node_index].terminal = binding;
        }
    }

    dispatch->param_route_trie_roots = roots;
    dispatch->param_route_trie_root_count = root_count;
    dispatch->param_route_trie_nodes = nodes;
    dispatch->param_route_trie_node_count = node_count;
    dispatch->param_route_trie_edges = edges;
    dispatch->param_route_trie_edge_count = edge_count;
    return sl_status_ok();
}

static SlStatus sl_http_route_table_build_exact_index(SlArena* arena, SlHttpRouteBinding* bindings,
                                                      size_t route_count,
                                                      SlHttpDispatchTable* dispatch)
{
    SlSlice bucket_slice = {0};
    SlHttpRouteBinding** buckets = NULL;
    size_t exact_count = 0U;
    size_t param_start = route_count;
    size_t bucket_count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || dispatch == NULL || (route_count != 0U && bindings == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < route_count; index += 1U) {
        if (sl_http_route_binding_exact_static(&bindings[index])) {
            exact_count += 1U;
            continue;
        }
        if (param_start == route_count) {
            param_start = index;
        }
    }

    if (param_start < route_count) {
        dispatch->param_routes = &bindings[param_start];
        dispatch->param_route_count = route_count - param_start;
        status = sl_http_route_table_build_param_index(arena, dispatch->param_routes,
                                                       dispatch->param_route_count, dispatch);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_route_table_build_param_trie(arena, dispatch->param_routes,
                                                      dispatch->param_route_count, dispatch);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (exact_count == 0U) {
        return sl_status_ok();
    }
    if (!sl_http_dispatch_next_bucket_count(exact_count, &bucket_count)) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_arena_array_alloc(arena, bucket_count, sizeof(SlHttpRouteBinding*),
                                  _Alignof(SlHttpRouteBinding*), &bucket_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    buckets = (SlHttpRouteBinding**)bucket_slice.ptr;
    for (index = 0U; index < bucket_count; index += 1U) {
        buckets[index] = NULL;
    }

    for (index = 0U; index < route_count; index += 1U) {
        if (!sl_http_route_binding_exact_static(&bindings[index])) {
            continue;
        }
        status = sl_http_route_table_insert_exact(buckets, bucket_count, &bindings[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    dispatch->exact_route_buckets = (const SlHttpRouteBinding**)buckets;
    dispatch->exact_route_bucket_count = bucket_count;
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
        if (!sl_http_plan_route_is_runnable(route)) {
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

        status =
            sl_http_dispatch_method_from_plan(route->method, &entries[entry_count].binding.method);
        if (!sl_status_is_ok(status)) {
            return sl_http_dispatch_unsupported_method(arena, out_diag);
        }
        entries[entry_count].binding.pattern = &entries[entry_count].pattern;
        entries[entry_count].binding.handler_id = route->handler_id;
        entries[entry_count].binding.handler = handler;
        entries[entry_count].binding.route_index = index;
        entries[entry_count].source_order = index;
        entries[entry_count].has_params = entries[entry_count].binding.pattern->param_count != 0U;
        for (size_t segment_index = 0U; segment_index < entries[entry_count].pattern.segment_count;
             segment_index += 1U)
        {
            SlRouteSegment* segment = &entries[entry_count].pattern.segments[segment_index];
            if (segment->kind == SL_ROUTE_SEGMENT_STATIC) {
                entries[entry_count].static_segment_count += 1U;
            }
            else if (segment->kind == SL_ROUTE_SEGMENT_PARAM &&
                     segment->param_kind != SL_ROUTE_PARAM_STRING)
            {
                entries[entry_count].constrained_param_count += 1U;
            }
        }
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
        if (sl_http_plan_route_is_runnable(&plan->routes[index])) {
            route_count += 1U;
        }
    }

    *out_route_count = route_count;
    return sl_status_ok();
}

typedef struct SlHttpDispatchAllowSet
{
    bool get;
    bool head;
    bool post;
    bool put;
    bool patch;
    bool delete_;
    bool options;
} SlHttpDispatchAllowSet;

static void sl_http_dispatch_allow_set_add(SlHttpDispatchAllowSet* methods, SlHttpMethod method)
{
    if (methods == NULL) {
        return;
    }
    switch (method) {
    case SL_HTTP_METHOD_GET:
        methods->get = true;
        methods->head = true;
        break;
    case SL_HTTP_METHOD_HEAD:
        methods->head = true;
        break;
    case SL_HTTP_METHOD_POST:
        methods->post = true;
        break;
    case SL_HTTP_METHOD_PUT:
        methods->put = true;
        break;
    case SL_HTTP_METHOD_PATCH:
        methods->patch = true;
        break;
    case SL_HTTP_METHOD_DELETE:
        methods->delete_ = true;
        break;
    case SL_HTTP_METHOD_OPTIONS:
        methods->options = true;
        break;
    case SL_HTTP_METHOD_UNKNOWN:
    default:
        break;
    }
}

static bool sl_http_dispatch_allow_set_empty(const SlHttpDispatchAllowSet* methods)
{
    return methods == NULL || (!methods->get && !methods->head && !methods->post && !methods->put &&
                               !methods->patch && !methods->delete_ && !methods->options);
}

static SlStatus sl_http_dispatch_allow_append(SlStringBuilder* builder, const char* method,
                                              bool* wrote_any)
{
    SlStatus status;

    if (builder == NULL || method == NULL || wrote_any == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (*wrote_any) {
        status = sl_string_builder_append_cstr(builder, ", ");
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_string_builder_append_cstr(builder, method);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *wrote_any = true;
    return sl_status_ok();
}

static SlStatus sl_http_dispatch_format_allow_header(SlArena* arena,
                                                     const SlHttpDispatchAllowSet* methods,
                                                     SlStr* out_allow)
{
    SlStringBuilder builder = {0};
    bool wrote_any = false;
    SlStatus status;

    if (out_allow != NULL) {
        *out_allow = sl_str_empty();
    }
    if (arena == NULL || methods == NULL || out_allow == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_http_dispatch_allow_set_empty(methods)) {
        return sl_status_ok();
    }

    status = sl_string_builder_init_arena(&builder, arena, 32U, 128U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
#define SL_HTTP_DISPATCH_APPEND_ALLOW(flag, token)                                                 \
    do {                                                                                           \
        if ((flag)) {                                                                              \
            status = sl_http_dispatch_allow_append(&builder, (token), &wrote_any);                 \
            if (!sl_status_is_ok(status)) {                                                        \
                return status;                                                                     \
            }                                                                                      \
        }                                                                                          \
    } while (0)

    SL_HTTP_DISPATCH_APPEND_ALLOW(methods->get, "GET");
    SL_HTTP_DISPATCH_APPEND_ALLOW(methods->head, "HEAD");
    SL_HTTP_DISPATCH_APPEND_ALLOW(methods->post, "POST");
    SL_HTTP_DISPATCH_APPEND_ALLOW(methods->put, "PUT");
    SL_HTTP_DISPATCH_APPEND_ALLOW(methods->patch, "PATCH");
    SL_HTTP_DISPATCH_APPEND_ALLOW(methods->delete_, "DELETE");
    SL_HTTP_DISPATCH_APPEND_ALLOW(methods->options, "OPTIONS");

#undef SL_HTTP_DISPATCH_APPEND_ALLOW

    *out_allow = sl_string_builder_view(&builder);
    return sl_status_ok();
}

SlStatus sl_http_dispatch_allow_header_for_path(SlArena* arena,
                                                const SlHttpDispatchTable* dispatch_table,
                                                SlStr path, SlStr* out_allow)
{
    SlArenaMark mark = {0};
    SlHttpDispatchAllowSet methods = {0};
    size_t index = 0U;
    SlStatus status;

    if (out_allow != NULL) {
        *out_allow = sl_str_empty();
    }
    if (arena == NULL || dispatch_table == NULL || out_allow == NULL || path.ptr == NULL ||
        path.length == 0U || path.ptr[0] != '/')
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_http_dispatch_validate_table(dispatch_table);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    mark = sl_arena_mark(arena);
    for (index = 0U; index < dispatch_table->route_count; index += 1U) {
        const SlHttpRouteBinding* binding = &dispatch_table->routes[index];
        SlRouteMatch match = {0};

        if (!sl_http_dispatch_binding_method_runnable(binding->method) ||
            binding->pattern == NULL || !sl_handler_id_valid(binding->handler_id))
        {
            status = sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            goto cleanup;
        }

        status = sl_route_pattern_match(arena, binding->pattern, path, &match);
        if (!sl_status_is_ok(status)) {
            goto cleanup;
        }
        if (match.matched) {
            sl_http_dispatch_allow_set_add(&methods, binding->method);
        }
    }

cleanup: {
    SlStatus reset_status = sl_arena_reset_to(arena, mark);
    if (!sl_status_is_ok(status)) {
        (void)reset_status;
        return status;
    }
    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }
}
    return sl_http_dispatch_format_allow_header(arena, &methods, out_allow);
}

static bool sl_http_dispatch_url_unreserved(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

static SlStatus sl_http_dispatch_url_append_encoded(SlStringBuilder* builder, SlStr value)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t index = 0U;
    SlStatus status;

    if (builder == NULL || (value.length != 0U && value.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (sl_http_dispatch_url_unreserved((char)ch)) {
            status = sl_string_builder_append_char(builder, (char)ch);
        }
        else {
            status = sl_string_builder_append_char(builder, '%');
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, hex[(ch >> 4U) & 0x0FU]);
            }
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, hex[ch & 0x0FU]);
            }
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

static const SlRouteParam* sl_http_dispatch_find_url_param(const SlRouteParam* params,
                                                           size_t param_count, SlStr name)
{
    size_t index = 0U;

    if (params == NULL) {
        return NULL;
    }
    for (index = 0U; index < param_count; index += 1U) {
        if (sl_str_equal(params[index].name, name)) {
            return &params[index];
        }
    }
    return NULL;
}

static bool sl_http_dispatch_pattern_has_param(const SlRoutePattern* pattern, SlStr name)
{
    size_t index = 0U;

    if (pattern == NULL) {
        return false;
    }
    for (index = 0U; index < pattern->segment_count; index += 1U) {
        const SlRouteSegment* segment = &pattern->segments[index];
        if (segment->kind == SL_ROUTE_SEGMENT_PARAM && sl_str_equal(segment->param_name, name)) {
            return true;
        }
    }
    return false;
}

static bool sl_http_dispatch_url_params_valid(const SlRoutePattern* pattern,
                                              const SlRouteParam* params, size_t param_count)
{
    size_t index = 0U;
    size_t previous = 0U;

    if (pattern == NULL || (param_count != 0U && params == NULL) ||
        param_count != pattern->param_count)
    {
        return false;
    }
    for (index = 0U; index < param_count; index += 1U) {
        if (sl_str_is_empty(params[index].name) ||
            !sl_http_dispatch_pattern_has_param(pattern, params[index].name))
        {
            return false;
        }
        for (previous = 0U; previous < index; previous += 1U) {
            if (sl_str_equal(params[index].name, params[previous].name)) {
                return false;
            }
        }
    }
    return true;
}

static const SlHttpRouteBinding*
sl_http_dispatch_find_named_binding(const SlHttpDispatchTable* dispatch_table, SlStr route_name)
{
    size_t index = 0U;

    if (dispatch_table == NULL || dispatch_table->plan == NULL || sl_str_is_empty(route_name)) {
        return NULL;
    }
    for (index = 0U; index < dispatch_table->route_count; index += 1U) {
        const SlHttpRouteBinding* binding = &dispatch_table->routes[index];
        if (binding->route_index < dispatch_table->plan->route_count &&
            sl_str_equal(dispatch_table->plan->routes[binding->route_index].name, route_name))
        {
            return binding;
        }
    }
    return NULL;
}

SlStatus sl_http_dispatch_generate_url(SlArena* arena, const SlHttpDispatchTable* dispatch_table,
                                       SlStr route_name, const SlRouteParam* params,
                                       size_t param_count, SlStr* out_url)
{
    SlArenaMark mark = {0};
    SlStringBuilder builder = {0};
    const SlHttpRouteBinding* binding = NULL;
    SlRouteMatch match = {0};
    SlStr url = {0};
    size_t index = 0U;
    SlStatus status;

    if (out_url != NULL) {
        *out_url = sl_str_empty();
    }
    if (arena == NULL || dispatch_table == NULL || out_url == NULL ||
        (param_count != 0U && params == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_http_dispatch_validate_table(dispatch_table);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    binding = sl_http_dispatch_find_named_binding(dispatch_table, route_name);
    if (binding == NULL || binding->pattern == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    if (!sl_http_dispatch_url_params_valid(binding->pattern, params, param_count)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(arena);
    status = sl_string_builder_init_arena(&builder, arena, binding->pattern->source.length + 16U,
                                          SL_HTTP_DEFAULT_MAX_TARGET_LENGTH);
    if (!sl_status_is_ok(status)) {
        goto cleanup;
    }
    if (binding->pattern->segment_count == 0U) {
        status = sl_string_builder_append_char(&builder, '/');
        if (!sl_status_is_ok(status)) {
            goto cleanup;
        }
    }
    for (index = 0U; index < binding->pattern->segment_count; index += 1U) {
        const SlRouteSegment* segment = &binding->pattern->segments[index];
        status = sl_string_builder_append_char(&builder, '/');
        if (!sl_status_is_ok(status)) {
            goto cleanup;
        }
        if (segment->kind == SL_ROUTE_SEGMENT_STATIC) {
            status = sl_string_builder_append_str(&builder, segment->text);
        }
        else {
            const SlRouteParam* param =
                sl_http_dispatch_find_url_param(params, param_count, segment->param_name);
            if (param == NULL || sl_str_is_empty(param->value)) {
                status = sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            else {
                status = sl_http_dispatch_url_append_encoded(&builder, param->value);
            }
        }
        if (!sl_status_is_ok(status)) {
            goto cleanup;
        }
    }

    url = sl_string_builder_view(&builder);
    status = sl_route_pattern_match(arena, binding->pattern, url, &match);
    if (!sl_status_is_ok(status)) {
        goto cleanup;
    }
    if (!match.matched) {
        status = sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        goto cleanup;
    }

    *out_url = url;
    return sl_status_ok();

cleanup: {
    SlStatus reset_status = sl_arena_reset_to(arena, mark);
    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }
}
    return status;
}

SlStatus sl_http_route_table_build(SlArena* arena, const SlPlan* plan, SlHttpRouteTable* out_table,
                                   SlDiag* out_diag)
{
    SlArenaMark mark = {0};
    SlArenaMark duplicate_mark = {0};
    SlHttpRouteTableEntry* entries = NULL;
    SlHttpRouteBinding* bindings = NULL;
    bool has_duplicate_routes = false;
    size_t param_start = 0U;
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

    duplicate_mark = sl_arena_mark(arena);
    status = sl_http_route_table_has_duplicate_plan_routes(arena, plan, &has_duplicate_routes);
    if (!sl_status_is_ok(status)) {
        sl_arena_reset_to(arena, duplicate_mark);
        return status;
    }
    status = sl_arena_reset_to(arena, duplicate_mark);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (has_duplicate_routes) {
        return sl_http_route_table_duplicate_route(arena, out_diag);
    }
    if (sl_plan_has_duplicate_handler_ids(plan)) {
        return sl_http_route_table_duplicate_handler(arena, out_diag);
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
        out_table->dispatch.plan = plan;
        out_table->dispatch.dispatch_mode = sl_http_route_dispatch_mode_from_env();
        out_table->dispatch.runtime_reserved0 = 0U;
        out_table->route_count = 0U;
        return sl_status_ok();
    }
    status =
        sl_http_route_table_fill_entries(arena, plan, entries, &runnable_route_count, out_diag);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    param_start = sl_http_route_table_partition_exact_first(entries, runnable_route_count);
    sl_http_route_table_sort_param_entries(&entries[param_start],
                                           runnable_route_count - param_start);
    for (index = 0U; index < runnable_route_count; index += 1U) {
        entries[index].binding.pattern = &entries[index].pattern;
        bindings[index] = entries[index].binding;
        out_table->dispatch.runtime_reserved0 |=
            sl_http_dispatch_method_bit(bindings[index].method);
    }

    out_table->dispatch.routes = bindings;
    out_table->dispatch.route_count = runnable_route_count;
    out_table->dispatch.plan = plan;
    out_table->dispatch.dispatch_mode = sl_http_route_dispatch_mode_from_env();
    out_table->dispatch.handler_cache_trusted = true;
    status = sl_http_route_table_build_exact_index(arena, bindings, runnable_route_count,
                                                   &out_table->dispatch);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    out_table->dispatch.runtime_reserved0 |= SL_HTTP_DISPATCH_TABLE_VALIDATED_FLAG;
    out_table->route_count = runnable_route_count;
    return sl_status_ok();

failure:
    sl_arena_reset_to(arena, mark);
    *out_table = (SlHttpRouteTable){0};
    return status;
}

static const SlHttpRouteBinding*
sl_http_dispatch_find_exact_route_for_method(const SlHttpDispatchTable* dispatch_table,
                                             SlHttpMethod method, SlStr path)
{
    size_t mask = 0U;
    size_t hash = 0U;
    size_t probe = 0U;

    if (dispatch_table == NULL || dispatch_table->exact_route_bucket_count == 0U ||
        dispatch_table->exact_route_buckets == NULL ||
        !sl_http_dispatch_power_of_two(dispatch_table->exact_route_bucket_count))
    {
        return NULL;
    }

    mask = dispatch_table->exact_route_bucket_count - 1U;
    hash = sl_http_dispatch_exact_hash(method, path);
    for (probe = 0U; probe < dispatch_table->exact_route_bucket_count; probe += 1U) {
        const SlHttpRouteBinding* binding =
            dispatch_table->exact_route_buckets[(hash + probe) & mask];
        if (binding == NULL) {
            return NULL;
        }
        if (binding->method == method && binding->pattern != NULL &&
            sl_str_equal(binding->pattern->source, path))
        {
            return binding;
        }
    }

    return NULL;
}

static bool sl_http_dispatch_exact_path_has_other_method(const SlHttpDispatchTable* dispatch_table,
                                                         SlHttpMethod request_method, SlStr path)
{
    static const SlHttpMethod methods[] = {SL_HTTP_METHOD_GET,    SL_HTTP_METHOD_POST,
                                           SL_HTTP_METHOD_PUT,    SL_HTTP_METHOD_PATCH,
                                           SL_HTTP_METHOD_DELETE, SL_HTTP_METHOD_OPTIONS};
    size_t index = 0U;
    unsigned int request_bit = sl_http_dispatch_method_bit(request_method);
    unsigned int method_bits =
        dispatch_table->runtime_reserved0 & SL_HTTP_DISPATCH_TABLE_METHOD_MASK;

    if (method_bits != 0U && (method_bits & ~request_bit) == 0U) {
        return false;
    }

    for (index = 0U; index < sizeof(methods) / sizeof(methods[0]); index += 1U) {
        if (methods[index] == request_method) {
            continue;
        }
        if (sl_http_dispatch_find_exact_route_for_method(dispatch_table, methods[index], path) !=
            NULL)
        {
            return true;
        }
    }

    return false;
}

static SlStr sl_http_dispatch_first_path_segment(SlStr path)
{
    size_t end = 1U;

    if (path.ptr == NULL || path.length <= 1U || path.ptr[0] != '/') {
        return sl_str_empty();
    }

    while (end < path.length && path.ptr[end] != '/') {
        end += 1U;
    }
    return sl_str_from_parts(path.ptr + 1U, end - 1U);
}

static bool sl_http_dispatch_path_segment_int(SlStr segment)
{
    size_t index = 0U;

    if (segment.length == 0U || segment.ptr == NULL) {
        return false;
    }
    for (index = 0U; index < segment.length; index += 1U) {
        if (segment.ptr[index] < '0' || segment.ptr[index] > '9') {
            return false;
        }
    }
    return true;
}

static bool sl_http_dispatch_path_segment_alpha(SlStr segment)
{
    size_t index = 0U;

    if (segment.length == 0U || segment.ptr == NULL) {
        return false;
    }
    for (index = 0U; index < segment.length; index += 1U) {
        char byte = segment.ptr[index];
        if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z'))) {
            return false;
        }
    }
    return true;
}

static bool sl_http_dispatch_path_segment_uuid(SlStr segment)
{
    static const size_t dash_positions[] = {8U, 13U, 18U, 23U};
    size_t dash_index = 0U;
    size_t index = 0U;

    if (segment.length != 36U || segment.ptr == NULL) {
        return false;
    }
    for (index = 0U; index < segment.length; index += 1U) {
        char byte = segment.ptr[index];
        if (dash_index < sizeof(dash_positions) / sizeof(dash_positions[0]) &&
            index == dash_positions[dash_index])
        {
            if (byte != '-') {
                return false;
            }
            dash_index += 1U;
            continue;
        }
        if (!((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'F') ||
              (byte >= 'a' && byte <= 'f')))
        {
            return false;
        }
    }
    return true;
}

static bool sl_http_dispatch_path_segment_float(SlStr segment)
{
    size_t index = 0U;
    bool saw_digit = false;
    bool saw_dot = false;

    if (segment.length == 0U || segment.ptr == NULL) {
        return false;
    }
    for (index = 0U; index < segment.length; index += 1U) {
        char byte = segment.ptr[index];
        if (byte >= '0' && byte <= '9') {
            saw_digit = true;
            continue;
        }
        if (byte == '.' && !saw_dot) {
            saw_dot = true;
            continue;
        }
        return false;
    }
    return saw_digit && saw_dot;
}

static bool sl_http_dispatch_path_segment_matches_kind(SlRouteParamKind kind, SlStr segment)
{
    if (kind == SL_ROUTE_PARAM_STRING) {
        return segment.length != 0U;
    }
    if (kind == SL_ROUTE_PARAM_INT) {
        return sl_http_dispatch_path_segment_int(segment);
    }
    if (kind == SL_ROUTE_PARAM_UUID) {
        return sl_http_dispatch_path_segment_uuid(segment);
    }
    if (kind == SL_ROUTE_PARAM_ALPHA) {
        return sl_http_dispatch_path_segment_alpha(segment);
    }
    if (kind == SL_ROUTE_PARAM_FLOAT) {
        return sl_http_dispatch_path_segment_float(segment);
    }
    return false;
}

static bool sl_http_dispatch_pattern_could_match(const SlRoutePattern* pattern, SlStr path)
{
    size_t pattern_index = 0U;
    size_t path_pos = 1U;

    if (pattern == NULL || (pattern->segment_count != 0U && pattern->segments == NULL) ||
        path.ptr == NULL || path.length == 0U || path.ptr[0] != '/')
    {
        return false;
    }
    if (pattern->segment_count == 0U) {
        return path.length == 1U;
    }

    while (pattern_index < pattern->segment_count) {
        const SlRouteSegment* pattern_segment = &pattern->segments[pattern_index];
        size_t segment_start = path_pos;
        size_t segment_end = path_pos;
        SlStr path_segment = {0};

        if (path_pos >= path.length) {
            return false;
        }
        while (segment_end < path.length && path.ptr[segment_end] != '/') {
            segment_end += 1U;
        }

        path_segment = sl_str_from_parts(path.ptr + segment_start, segment_end - segment_start);
        if (path_segment.length == 0U) {
            return false;
        }

        if (pattern_segment->kind == SL_ROUTE_SEGMENT_STATIC) {
            if (!sl_str_equal(pattern_segment->text, path_segment)) {
                return false;
            }
        }
        else if (pattern_segment->kind == SL_ROUTE_SEGMENT_PARAM) {
            if (!sl_http_dispatch_path_segment_matches_kind(pattern_segment->param_kind,
                                                            path_segment))
            {
                return false;
            }
        }
        else {
            return false;
        }

        pattern_index += 1U;
        path_pos = segment_end == path.length ? segment_end : segment_end + 1U;
    }

    return path_pos == path.length && path.ptr[path.length - 1U] != '/';
}

static SlStatus sl_http_dispatch_copy_route_params(SlArena* arena, const SlRouteParam* captures,
                                                   size_t capture_count, SlRouteParam** out)
{
    SlSlice param_slice = {0};

    if (arena == NULL || captures == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;
    if (capture_count == 0U) {
        return sl_status_ok();
    }

    SlStatus status = sl_arena_array_copy(arena, captures, capture_count, sizeof(SlRouteParam),
                                          _Alignof(SlRouteParam), &param_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = (SlRouteParam*)param_slice.ptr;
    return sl_status_ok();
}

static SlStatus sl_http_dispatch_match_trusted_pattern(SlArena* arena,
                                                       const SlRoutePattern* pattern, SlStr path,
                                                       SlRouteMatch* out_match)
{
    SlRouteParam captures[SL_ROUTE_MAX_PARAMS];
    size_t pattern_index = 0U;
    size_t path_pos = 1U;
    size_t param_index = 0U;

    if (out_match == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_match = (SlRouteMatch){0};
    if (arena == NULL || pattern == NULL ||
        (pattern->segment_count != 0U && pattern->segments == NULL) ||
        pattern->param_count > SL_ROUTE_MAX_PARAMS || path.ptr == NULL || path.length == 0U ||
        path.ptr[0] != '/')
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (pattern->segment_count == 0U) {
        out_match->matched = path.length == 1U;
        return sl_status_ok();
    }

    while (pattern_index < pattern->segment_count) {
        const SlRouteSegment* pattern_segment = &pattern->segments[pattern_index];
        size_t segment_start = path_pos;
        size_t segment_end = path_pos;
        SlStr path_segment = {0};

        if (path_pos >= path.length) {
            return sl_status_ok();
        }
        while (segment_end < path.length && path.ptr[segment_end] != '/') {
            segment_end += 1U;
        }

        path_segment = sl_str_from_parts(path.ptr + segment_start, segment_end - segment_start);
        if (path_segment.length == 0U) {
            return sl_status_ok();
        }

        if (pattern_segment->kind == SL_ROUTE_SEGMENT_STATIC) {
            if (!sl_str_equal(pattern_segment->text, path_segment)) {
                return sl_status_ok();
            }
        }
        else if (pattern_segment->kind == SL_ROUTE_SEGMENT_PARAM) {
            if (param_index >= SL_ROUTE_MAX_PARAMS ||
                !sl_http_dispatch_path_segment_matches_kind(pattern_segment->param_kind,
                                                            path_segment))
            {
                return sl_status_ok();
            }
            captures[param_index].name = pattern_segment->param_name;
            captures[param_index].value = path_segment;
            captures[param_index].kind = pattern_segment->param_kind;
            param_index += 1U;
        }
        else {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        pattern_index += 1U;
        path_pos = segment_end == path.length ? segment_end : segment_end + 1U;
    }

    if (path_pos != path.length || path.ptr[path.length - 1U] == '/') {
        return sl_status_ok();
    }

    SlRouteParam* copied_params = NULL;
    SlStatus status =
        sl_http_dispatch_copy_route_params(arena, captures, param_index, &copied_params);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    out_match->matched = true;
    out_match->params = copied_params;
    out_match->param_count = param_index;
    return sl_status_ok();
}

static const SlHttpRouteCandidateBucket*
sl_http_dispatch_find_param_bucket(const SlHttpDispatchTable* dispatch_table, SlHttpMethod method,
                                   SlStr first_static_segment)
{
    size_t index = 0U;

    if (dispatch_table == NULL || dispatch_table->param_route_bucket_count == 0U ||
        dispatch_table->param_route_buckets == NULL)
    {
        return NULL;
    }

    if (dispatch_table->param_route_bucket_slot_count != 0U &&
        dispatch_table->param_route_bucket_slots != NULL &&
        sl_http_dispatch_power_of_two(dispatch_table->param_route_bucket_slot_count))
    {
        size_t mask = dispatch_table->param_route_bucket_slot_count - 1U;
        size_t hash = sl_http_dispatch_param_bucket_hash(method, first_static_segment);
        size_t probe = 0U;

        for (probe = 0U; probe < dispatch_table->param_route_bucket_slot_count; probe += 1U) {
            const SlHttpRouteCandidateBucket* bucket =
                dispatch_table->param_route_bucket_slots[(hash + probe) & mask];
            if (bucket == NULL) {
                return NULL;
            }
            if (bucket->method == method &&
                sl_str_equal(bucket->first_static_segment, first_static_segment))
            {
                return bucket;
            }
        }
        return NULL;
    }

    for (index = 0U; index < dispatch_table->param_route_bucket_count; index += 1U) {
        const SlHttpRouteCandidateBucket* bucket = &dispatch_table->param_route_buckets[index];
        if (bucket->method == method &&
            sl_str_equal(bucket->first_static_segment, first_static_segment))
        {
            return bucket;
        }
    }

    return NULL;
}

static const SlHttpRouteBinding*
sl_http_dispatch_next_param_candidate(const SlHttpRouteCandidateBucket* first, size_t* first_index,
                                      const SlHttpRouteCandidateBucket* generic,
                                      size_t* generic_index)
{
    const SlHttpRouteBinding* first_route = NULL;
    const SlHttpRouteBinding* generic_route = NULL;

    if (first_index != NULL && first != NULL && first->routes != NULL &&
        *first_index < first->route_count)
    {
        first_route = first->routes[*first_index];
    }
    if (generic_index != NULL && generic != NULL && generic->routes != NULL &&
        *generic_index < generic->route_count)
    {
        generic_route = generic->routes[*generic_index];
    }

    if (first_route == NULL && generic_route == NULL) {
        return NULL;
    }
    if (first_route == NULL) {
        *generic_index += 1U;
        return generic_route;
    }
    if (generic_route == NULL) {
        *first_index += 1U;
        return first_route;
    }
    if (first_route <= generic_route) {
        *first_index += 1U;
        return first_route;
    }

    *generic_index += 1U;
    return generic_route;
}

static const SlHttpRouteTrieRoot*
sl_http_dispatch_find_param_trie_root(const SlHttpDispatchTable* dispatch_table,
                                      SlHttpMethod method)
{
    size_t index = 0U;

    if (dispatch_table == NULL || dispatch_table->param_route_trie_roots == NULL) {
        return NULL;
    }

    for (index = 0U; index < dispatch_table->param_route_trie_root_count; index += 1U) {
        const SlHttpRouteTrieRoot* root = &dispatch_table->param_route_trie_roots[index];
        if (root->method == method) {
            return root;
        }
    }
    return NULL;
}

static const SlHttpRouteTrieEdge*
sl_http_dispatch_find_best_trie_edge(const SlHttpDispatchTable* dispatch_table,
                                     const SlHttpRouteTrieNode* node, SlStr path_segment)
{
    size_t edge_index = SL_HTTP_ROUTE_TRIE_NONE;
    const SlHttpRouteTrieEdge* constrained = NULL;
    const SlHttpRouteTrieEdge* string_param = NULL;

    if (dispatch_table == NULL || node == NULL || dispatch_table->param_route_trie_edges == NULL) {
        return NULL;
    }

    edge_index = node->first_edge;
    while (edge_index != SL_HTTP_ROUTE_TRIE_NONE) {
        const SlHttpRouteTrieEdge* edge = NULL;
        if (edge_index >= dispatch_table->param_route_trie_edge_count) {
            return NULL;
        }
        edge = &dispatch_table->param_route_trie_edges[edge_index];
        if (edge->kind == SL_ROUTE_SEGMENT_STATIC) {
            if (sl_str_equal(edge->text, path_segment)) {
                return edge;
            }
        }
        else if (edge->kind == SL_ROUTE_SEGMENT_PARAM) {
            if (edge->param_kind != SL_ROUTE_PARAM_STRING && constrained == NULL &&
                sl_http_dispatch_path_segment_matches_kind(edge->param_kind, path_segment))
            {
                constrained = edge;
            }
            else if (edge->param_kind == SL_ROUTE_PARAM_STRING && string_param == NULL &&
                     sl_http_dispatch_path_segment_matches_kind(edge->param_kind, path_segment))
            {
                string_param = edge;
            }
        }
        edge_index = edge->next_edge;
    }
    return constrained != NULL ? constrained : string_param;
}

static SlStatus
sl_http_dispatch_match_param_trie_for_method(const SlHttpDispatchTable* dispatch_table,
                                             const SlHttpRequestHead* request, SlHttpMethod method,
                                             const SlHttpRouteBinding** out_binding)
{
    const SlHttpRouteTrieRoot* root = NULL;
    size_t node_index = 0U;
    size_t path_pos = 1U;

    if (dispatch_table == NULL || request == NULL || out_binding == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_binding = NULL;
    if (dispatch_table->param_route_trie_node_count == 0U ||
        dispatch_table->param_route_trie_nodes == NULL)
    {
        return sl_status_ok();
    }

    root = sl_http_dispatch_find_param_trie_root(dispatch_table, method);
    if (root == NULL || root->root_index >= dispatch_table->param_route_trie_node_count ||
        request->path.ptr == NULL || request->path.length == 0U || request->path.ptr[0] != '/')
    {
        return sl_status_ok();
    }

    node_index = root->root_index;
    if (request->path.length == 1U) {
        *out_binding = dispatch_table->param_route_trie_nodes[node_index].terminal;
        return sl_status_ok();
    }

    while (path_pos < request->path.length) {
        const SlHttpRouteTrieNode* node = NULL;
        const SlHttpRouteTrieEdge* edge = NULL;
        size_t segment_start = path_pos;
        size_t segment_end = path_pos;
        SlStr path_segment = {0};

        while (segment_end < request->path.length && request->path.ptr[segment_end] != '/') {
            segment_end += 1U;
        }
        path_segment =
            sl_str_from_parts(request->path.ptr + segment_start, segment_end - segment_start);
        if (path_segment.length == 0U || node_index >= dispatch_table->param_route_trie_node_count)
        {
            return sl_status_ok();
        }

        node = &dispatch_table->param_route_trie_nodes[node_index];
        edge = sl_http_dispatch_find_best_trie_edge(dispatch_table, node, path_segment);
        if (edge == NULL || edge->child_index >= dispatch_table->param_route_trie_node_count) {
            return sl_status_ok();
        }

        node_index = edge->child_index;
        path_pos = segment_end == request->path.length ? segment_end : segment_end + 1U;
    }

    if (request->path.ptr[request->path.length - 1U] == '/' ||
        node_index >= dispatch_table->param_route_trie_node_count)
    {
        return sl_status_ok();
    }

    *out_binding = dispatch_table->param_route_trie_nodes[node_index].terminal;
    return sl_status_ok();
}

static bool sl_http_dispatch_binding_valid(const SlHttpRouteBinding* binding)
{
    return binding != NULL && sl_http_dispatch_binding_method_runnable(binding->method) &&
           binding->pattern != NULL && sl_handler_id_valid(binding->handler_id);
}

static SlStatus sl_http_dispatch_find_param_route_for_method(
    const SlHttpDispatchTable* dispatch_table, const SlHttpRequestHead* request,
    SlHttpMethod method, SlArena* route_match_arena, SlRouteMatch* out_route_match,
    bool* out_has_route_match, const SlHttpRouteBinding** out_binding)
{
    SlStr first_segment = sl_http_dispatch_first_path_segment(request->path);
    const SlHttpRouteCandidateBucket* first_bucket =
        sl_http_dispatch_find_param_bucket(dispatch_table, method, first_segment);
    const SlHttpRouteCandidateBucket* generic_bucket =
        sl_http_dispatch_find_param_bucket(dispatch_table, method, sl_str_empty());
    size_t first_index = 0U;
    size_t generic_index = 0U;

    if (out_binding == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_binding = NULL;
    if (out_route_match != NULL) {
        *out_route_match = (SlRouteMatch){0};
    }
    if (out_has_route_match != NULL) {
        *out_has_route_match = false;
    }

    if (dispatch_table->param_route_trie_node_count != 0U) {
        const SlHttpRouteBinding* trie_binding = NULL;
        SlStatus status = sl_http_dispatch_match_param_trie_for_method(dispatch_table, request,
                                                                       method, &trie_binding);
        if (!sl_status_is_ok(status) || trie_binding == NULL) {
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        else {
            if (!sl_http_dispatch_binding_valid(trie_binding)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            *out_binding = trie_binding;
            if (dispatch_table->handler_cache_trusted && route_match_arena != NULL &&
                out_route_match != NULL && out_has_route_match != NULL)
            {
                SlRouteMatch route_match = {0};
                status = sl_http_dispatch_match_trusted_pattern(
                    route_match_arena, trie_binding->pattern, request->path, &route_match);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
                if (!route_match.matched) {
                    return sl_status_from_code(SL_STATUS_INTERNAL);
                }
                *out_route_match = route_match;
                *out_has_route_match = true;
            }
            return sl_status_ok();
        }
    }

    if (sl_str_is_empty(first_segment)) {
        first_bucket = NULL;
    }

    for (;;) {
        const SlHttpRouteBinding* binding = sl_http_dispatch_next_param_candidate(
            first_bucket, &first_index, generic_bucket, &generic_index);
        if (binding == NULL) {
            return sl_status_ok();
        }
        if (!sl_http_dispatch_binding_valid(binding)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (dispatch_table->handler_cache_trusted && route_match_arena != NULL &&
            out_route_match != NULL && out_has_route_match != NULL)
        {
            SlRouteMatch route_match = {0};
            SlStatus status = sl_http_dispatch_match_trusted_pattern(
                route_match_arena, binding->pattern, request->path, &route_match);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            if (route_match.matched) {
                *out_binding = binding;
                *out_route_match = route_match;
                *out_has_route_match = true;
                return sl_status_ok();
            }
            continue;
        }
        if (sl_http_dispatch_pattern_could_match(binding->pattern, request->path)) {
            *out_binding = binding;
            return sl_status_ok();
        }
    }
}

static SlStatus sl_http_dispatch_param_path_has_method(const SlHttpDispatchTable* dispatch_table,
                                                       const SlHttpRequestHead* request,
                                                       SlHttpMethod method, bool* out_has_method)
{
    const SlHttpRouteBinding* binding = NULL;
    SlStatus status;

    if (out_has_method == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_has_method = false;
    status = sl_http_dispatch_find_param_route_for_method(dispatch_table, request, method, NULL,
                                                          NULL, NULL, &binding);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out_has_method = binding != NULL;
    return sl_status_ok();
}

static SlStatus sl_http_dispatch_param_path_has_other_method(
    const SlHttpDispatchTable* dispatch_table, const SlHttpRequestHead* request,
    SlHttpMethod request_method, bool* out_has_other_method)
{
    static const SlHttpMethod methods[] = {SL_HTTP_METHOD_GET,    SL_HTTP_METHOD_POST,
                                           SL_HTTP_METHOD_PUT,    SL_HTTP_METHOD_PATCH,
                                           SL_HTTP_METHOD_DELETE, SL_HTTP_METHOD_OPTIONS};
    size_t index = 0U;
    unsigned int method_bits =
        dispatch_table->runtime_reserved0 & SL_HTTP_DISPATCH_TABLE_METHOD_MASK;

    if (out_has_other_method == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_has_other_method = false;
    if (method_bits != 0U && (method_bits & ~sl_http_dispatch_method_bit(request_method)) == 0U) {
        return sl_status_ok();
    }
    for (index = 0U; index < sizeof(methods) / sizeof(methods[0]); index += 1U) {
        bool has_method = false;
        if (methods[index] == request_method) {
            continue;
        }
        SlStatus status = sl_http_dispatch_param_path_has_method(dispatch_table, request,
                                                                 methods[index], &has_method);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (has_method) {
            *out_has_other_method = true;
            return sl_status_ok();
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http_dispatch_find_route_linear(const SlHttpDispatchTable* dispatch_table,
                                                   const SlHttpRequestHead* request,
                                                   const SlHttpRouteBinding** out_binding,
                                                   bool* out_method_mismatch)
{
    size_t index = 0U;

    if (dispatch_table == NULL || request == NULL || out_binding == NULL ||
        out_method_mismatch == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < dispatch_table->route_count; index += 1U) {
        const SlHttpRouteBinding* binding = &dispatch_table->routes[index];
        if (!sl_http_dispatch_binding_valid(binding)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (!sl_http_dispatch_pattern_could_match(binding->pattern, request->path)) {
            continue;
        }
        if (binding->method != sl_http_dispatch_route_match_method(request->method)) {
            *out_method_mismatch = true;
            continue;
        }
        *out_binding = binding;
        return sl_status_ok();
    }

    return sl_status_ok();
}

static SlStatus sl_http_dispatch_find_route_compiled(
    SlArena* arena, const SlHttpDispatchTable* dispatch_table, const SlHttpRequestHead* request,
    const SlHttpRouteBinding** out_binding, bool* out_method_mismatch,
    SlRouteMatch* out_route_match, bool* out_has_route_match)
{
    SlHttpMethod match_method = SL_HTTP_METHOD_UNKNOWN;

    if (arena == NULL || dispatch_table == NULL || request == NULL || out_binding == NULL ||
        out_method_mismatch == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_binding = NULL;
    *out_method_mismatch = false;
    if (out_route_match != NULL) {
        *out_route_match = (SlRouteMatch){0};
    }
    if (out_has_route_match != NULL) {
        *out_has_route_match = false;
    }
    match_method = sl_http_dispatch_route_match_method(request->method);

    if (dispatch_table->exact_route_bucket_count != 0U) {
        const SlHttpRouteBinding* exact = sl_http_dispatch_find_exact_route_for_method(
            dispatch_table, match_method, request->path);
        if (exact != NULL) {
            *out_binding = exact;
            return sl_status_ok();
        }
        *out_method_mismatch = sl_http_dispatch_exact_path_has_other_method(
            dispatch_table, match_method, request->path);
    }

    if (dispatch_table->param_route_bucket_count != 0U) {
        bool param_method_mismatch = false;
        SlStatus status = sl_http_dispatch_find_param_route_for_method(
            dispatch_table, request, match_method, arena, out_route_match, out_has_route_match,
            out_binding);
        if (!sl_status_is_ok(status) || *out_binding != NULL) {
            return status;
        }
        status = sl_http_dispatch_param_path_has_other_method(dispatch_table, request, match_method,
                                                              &param_method_mismatch);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_method_mismatch = *out_method_mismatch || param_method_mismatch;
        return sl_status_ok();
    }

    if (dispatch_table->exact_route_bucket_count == 0U) {
        return sl_http_dispatch_find_route_linear(dispatch_table, request, out_binding,
                                                  out_method_mismatch);
    }

    for (size_t index = 0U; index < dispatch_table->param_route_count; index += 1U) {
        const SlHttpRouteBinding* binding = &dispatch_table->param_routes[index];
        if (!sl_http_dispatch_binding_valid(binding)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (binding->method != match_method) {
            if (sl_http_dispatch_pattern_could_match(binding->pattern, request->path)) {
                *out_method_mismatch = true;
            }
            continue;
        }
        if (dispatch_table->handler_cache_trusted && out_route_match != NULL &&
            out_has_route_match != NULL)
        {
            SlRouteMatch route_match = {0};
            SlStatus status = sl_http_dispatch_match_trusted_pattern(arena, binding->pattern,
                                                                     request->path, &route_match);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            if (!route_match.matched) {
                continue;
            }
            *out_binding = binding;
            *out_route_match = route_match;
            *out_has_route_match = true;
            return sl_status_ok();
        }
        if (!sl_http_dispatch_pattern_could_match(binding->pattern, request->path)) {
            continue;
        }
        *out_binding = binding;
        return sl_status_ok();
    }

    return sl_status_ok();
}

static SlStatus
sl_http_dispatch_find_route(SlArena* arena, const SlHttpDispatchTable* dispatch_table,
                            const SlHttpRequestHead* request,
                            const SlHttpRouteBinding** out_binding, bool* out_method_mismatch,
                            SlRouteMatch* out_route_match, bool* out_has_route_match)
{
    SlHttpRouteDispatchMode mode = dispatch_table == NULL ? SL_HTTP_ROUTE_DISPATCH_MODE_COMPILED
                                                          : dispatch_table->dispatch_mode;

    if (mode == SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC) {
        if (out_route_match != NULL) {
            *out_route_match = (SlRouteMatch){0};
        }
        if (out_has_route_match != NULL) {
            *out_has_route_match = false;
        }
        return sl_http_dispatch_find_route_linear(dispatch_table, request, out_binding,
                                                  out_method_mismatch);
    }

    if (mode == SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE) {
        const SlHttpRouteBinding* compiled_binding = NULL;
        const SlHttpRouteBinding* classic_binding = NULL;
        bool compiled_method_mismatch = false;
        bool classic_method_mismatch = false;
        SlStatus status = sl_http_dispatch_find_route_compiled(
            arena, dispatch_table, request, &compiled_binding, &compiled_method_mismatch,
            out_route_match, out_has_route_match);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_dispatch_find_route_linear(dispatch_table, request, &classic_binding,
                                                    &classic_method_mismatch);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (compiled_binding != classic_binding ||
            compiled_method_mismatch != classic_method_mismatch)
        {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        *out_binding = compiled_binding;
        *out_method_mismatch = compiled_method_mismatch;
        return sl_status_ok();
    }

    return sl_http_dispatch_find_route_compiled(arena, dispatch_table, request, out_binding,
                                                out_method_mismatch, out_route_match,
                                                out_has_route_match);
}

SlStatus sl_http_dispatch_match_route(SlArena* arena, const SlPlan* plan,
                                      const SlHttpDispatchTable* dispatch_table,
                                      const SlHttpRequestHead* request,
                                      SlHttpDispatchRouteMatch* out_match, SlDiag* out_diag)
{
    const SlHttpRouteBinding* binding = NULL;
    SlRouteMatch route_match = {0};
    bool method_mismatch = false;
    bool has_route_match = false;
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_match != NULL) {
        *out_match = (SlHttpDispatchRouteMatch){0};
    }
    if (arena == NULL || plan == NULL || dispatch_table == NULL || request == NULL ||
        out_match == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_validate_table(dispatch_table);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_http_dispatch_request_method_runnable(request->method)) {
        return sl_http_dispatch_unsupported_method(arena, out_diag);
    }
    if (request->path.length == 0U || request->path.ptr == NULL || request->path.ptr[0] != '/') {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_find_route(arena, dispatch_table, request, &binding, &method_mismatch,
                                         &route_match, &has_route_match);
    if (!sl_status_is_ok(status)) {
        if (sl_status_code(status) == SL_STATUS_INTERNAL) {
            return sl_http_dispatch_write_diag(
                arena, out_diag, SL_DIAG_ROUTE_VALIDATE_MISMATCH,
                sl_http_dispatch_literal("compiled and classic route dispatch disagreed",
                                         sizeof("compiled and classic route dispatch disagreed") -
                                             1U),
                sl_http_dispatch_literal("rebuild route artifacts or force classic dispatch for "
                                         "triage",
                                         sizeof("rebuild route artifacts or force classic dispatch "
                                                "for triage") -
                                             1U),
                SL_STATUS_INTERNAL);
        }
        return status;
    }

    out_match->binding = binding;
    out_match->method_mismatch = method_mismatch;
    out_match->route_match = route_match;
    out_match->has_route_match = has_route_match;
    if (binding != NULL) {
        if (binding->route_index >= plan->route_count) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        out_match->route = &plan->routes[binding->route_index];
    }
    return sl_status_ok();
}

typedef struct SlHttpDispatchContextNeeds
{
    bool route_params;
    bool query_params;
    bool headers;
    bool body;
    bool header_facade;
    bool request;
    bool connection;
    bool signal;
    bool log;
    bool metadata;
} SlHttpDispatchContextNeeds;

static bool sl_http_dispatch_route_has_native_response(const SlPlanRoute* route);

static void sl_http_dispatch_context_needs_all(SlHttpDispatchContextNeeds* needs)
{
    if (needs == NULL) {
        return;
    }

    needs->route_params = true;
    needs->query_params = true;
    needs->headers = true;
    needs->body = true;
    needs->header_facade = true;
    needs->request = true;
    needs->connection = true;
    needs->signal = true;
    needs->log = true;
    needs->metadata = true;
}

static SlHttpDispatchContextNeeds sl_http_dispatch_context_needs(const SlPlanRoute* route)
{
    SlHttpDispatchContextNeeds needs = {0};
    size_t index = 0U;

    if (route == NULL || !sl_plan_route_has_bindings(route)) {
        sl_http_dispatch_context_needs_all(&needs);
        return needs;
    }

    for (index = 0U; index < route->binding_count; index += 1U) {
        switch (route->bindings[index].kind) {
        case SL_PLAN_REQUEST_BINDING_ROUTE:
            needs.route_params = true;
            break;
        case SL_PLAN_REQUEST_BINDING_QUERY:
            needs.query_params = true;
            break;
        case SL_PLAN_REQUEST_BINDING_HEADER:
            needs.headers = true;
            if (sl_str_is_empty(route->bindings[index].parameter)) {
                needs.header_facade = true;
            }
            else {
                needs.request = true;
            }
            break;
        case SL_PLAN_REQUEST_BINDING_BODY_JSON:
        case SL_PLAN_REQUEST_BINDING_BODY_FORM:
        case SL_PLAN_REQUEST_BINDING_BODY_MULTIPART:
            needs.body = true;
            needs.request = true;
            break;
        case SL_PLAN_REQUEST_BINDING_COOKIE:
            needs.headers = true;
            needs.request = true;
            break;
        case SL_PLAN_REQUEST_BINDING_CONTEXT:
            sl_http_dispatch_context_needs_all(&needs);
            break;
        case SL_PLAN_REQUEST_BINDING_INJECTION:
        case SL_PLAN_REQUEST_BINDING_UNKNOWN:
        default:
            break;
        }
    }

    return needs;
}

static SlStatus sl_http_dispatch_capture_route_params(SlArena* arena,
                                                      const SlHttpRouteBinding* binding,
                                                      const SlHttpRequestHead* request,
                                                      SlRouteMatch* out_match)
{
    if (out_match == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_match = (SlRouteMatch){0};
    if (binding == NULL || binding->pattern == NULL || request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (binding->pattern->param_count == 0U) {
        out_match->matched = true;
        return sl_status_ok();
    }
    return sl_route_pattern_match(arena, binding->pattern, request->path, out_match);
}

static bool sl_http_dispatch_route_has_native_response(const SlPlanRoute* route)
{
    return route != NULL && !sl_str_is_empty(route->native_response_kind) &&
           route->native_response_status >= 100U && route->native_response_status <= 599U;
}

static bool sl_http_dispatch_route_expects_json_body(const SlPlanRoute* route)
{
    size_t index = 0U;

    if (route == NULL) {
        return false;
    }
    if (route->json_request.mode == SL_PLAN_JSON_REQUEST_GENERIC ||
        route->json_request.mode == SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA ||
        route->json_request.mode == SL_PLAN_JSON_REQUEST_FALLBACK)
    {
        return true;
    }
    if (route->binding_count == 0U || route->bindings == NULL) {
        return false;
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        if (route->bindings[index].kind == SL_PLAN_REQUEST_BINDING_BODY_JSON) {
            return true;
        }
    }
    return false;
}

static bool sl_http_dispatch_route_uses_native_json_validation(const SlPlanRoute* route)
{
    return route != NULL && route->json_request.mode == SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA;
}

static const SlPlanSchema* sl_http_dispatch_find_schema(const SlPlan* plan, SlStr name)
{
    size_t index = 0U;

    if (plan == NULL || sl_str_is_empty(name) ||
        (plan->schema_count != 0U && plan->schemas == NULL))
    {
        return NULL;
    }
    for (index = 0U; index < plan->schema_count; index += 1U) {
        if (sl_str_equal(plan->schemas[index].name, name)) {
            return &plan->schemas[index];
        }
    }
    return NULL;
}

static SlHttpDispatchJsonMode sl_http_dispatch_json_mode(void)
{
    SlHttpDispatchJsonMode mode = SL_HTTP_DISPATCH_JSON_NATIVE;
    const char* value = NULL;
#ifdef _WIN32
    char buffer[16];
    size_t length = 0U;

    if (getenv_s(&length, buffer, sizeof(buffer), "SLOPPY_JSON_DISPATCH") == 0 && length > 0U) {
        value = buffer;
    }
#else
    value = getenv("SLOPPY_JSON_DISPATCH");
#endif

    if (value == NULL || value[0] == '\0' || strcmp(value, "native") == 0) {
        mode = SL_HTTP_DISPATCH_JSON_NATIVE;
    }
    else if (strcmp(value, "generic") == 0) {
        mode = SL_HTTP_DISPATCH_JSON_GENERIC;
    }
    else if (strcmp(value, "validate") == 0) {
        mode = SL_HTTP_DISPATCH_JSON_VALIDATE;
    }
    return mode;
}

static size_t sl_http_dispatch_effective_max_body_length(const SlPlanRoute* route,
                                                         const SlHttpDispatchContextSeed* seed)
{
    size_t seed_max_body_length = seed == NULL ? 0U : seed->max_body_length;
    size_t route_max_body_length = 0U;

    if (seed_max_body_length == SL_HTTP_DEFAULT_MAX_BODY_LENGTH) {
        seed_max_body_length = 0U;
    }
    if (route != NULL && sl_http_dispatch_route_expects_json_body(route) &&
        route->json_request.max_body_bytes != 0U)
    {
        route_max_body_length = route->json_request.max_body_bytes;
    }
    if (seed_max_body_length != 0U && route_max_body_length != 0U) {
        return route_max_body_length < seed_max_body_length ? route_max_body_length
                                                            : seed_max_body_length;
    }
    if (seed_max_body_length != 0U) {
        return seed_max_body_length;
    }
    if (route_max_body_length != 0U) {
        return route_max_body_length;
    }
    return SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
}

static SlStatus sl_http_dispatch_native_response(const SlPlanRoute* route,
                                                 SlEngineResult* out_result)
{
    SlBytes body = {0};

    if (route == NULL || out_result == NULL || !sl_http_dispatch_route_has_native_response(route)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    body = sl_bytes_from_parts((const unsigned char*)route->native_response_body.ptr,
                               route->native_response_body.length);
    *out_result = (SlEngineResult){0};
    if (sl_str_equal(route->native_response_kind, sl_str_from_cstr("text"))) {
        out_result->kind = SL_ENGINE_RESULT_TEXT;
        out_result->payload_kind = SL_ENGINE_RESULT_PAYLOAD_RESPONSE;
        out_result->response =
            sl_http_response_text(route->native_response_status, route->native_response_body);
        if (!sl_str_is_empty(route->native_response_content_type)) {
            out_result->response.content_type = route->native_response_content_type;
        }
        return sl_status_ok();
    }
    if (sl_str_equal(route->native_response_kind, sl_str_from_cstr("json"))) {
        out_result->kind = SL_ENGINE_RESULT_JSON;
        out_result->payload_kind = SL_ENGINE_RESULT_PAYLOAD_RESPONSE;
        out_result->response = sl_http_response_json(route->native_response_status, body);
        if (!sl_str_is_empty(route->native_response_content_type)) {
            out_result->response.content_type = route->native_response_content_type;
        }
        return sl_status_ok();
    }

    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

static void sl_http_dispatch_materialize_text_result(SlEngineResult* result)
{
    SlStr text = sl_str_empty();

    if (result == NULL || result->kind != SL_ENGINE_RESULT_TEXT ||
        result->payload_kind != SL_ENGINE_RESULT_PAYLOAD_TEXT)
    {
        return;
    }

    text = result->text;
    result->payload_kind = SL_ENGINE_RESULT_PAYLOAD_RESPONSE;
    result->response = sl_http_response_text(200U, text);
}

static SlStatus sl_http_dispatch_request_core(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                              const SlHttpDispatchTable* dispatch_table,
                                              const SlHttpRequestHead* request,
                                              const SlHttpDispatchContextSeed* seed,
                                              SlEngineResult* out_result, SlDiag* out_diag)
{
    const SlHttpRouteBinding* binding = NULL;
    const SlPlanHandler* handler = NULL;
    const SlPlanRoute* validation_route = NULL;
    SlRouteMatch route_match = {0};
    SlHttpQuery query = {0};
    SlHttpDispatchContextNeeds needs = {0};
    SlHttpRequestContext request_context = {0};
    SlHttpDispatchJsonMode json_mode = SL_HTTP_DISPATCH_JSON_NATIVE;
    bool use_native_json_validation = false;
    SlStr breadcrumb_path = sl_str_empty();
    bool method_mismatch = false;
    bool route_match_captured = false;
    bool use_cached_handler = false;
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
    if (request->path.length != 0U && request->path.ptr != NULL && request->path.ptr[0] == '/') {
        breadcrumb_path = request->path;
    }
    sl_http_dispatch_record_breadcrumb(
        SL_DIAG_SUBSYSTEM_HTTP, SL_BREADCRUMB_EVENT_HTTP_REQUEST_START, SL_STATUS_OK,
        seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id, 0U, 0U,
        breadcrumb_path);

    status = sl_http_dispatch_validate_table(dispatch_table);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_http_dispatch_request_method_runnable(request->method)) {
        sl_http_dispatch_record_breadcrumb(
            SL_DIAG_SUBSYSTEM_HTTP, SL_BREADCRUMB_EVENT_METHOD_MISMATCH, SL_STATUS_UNSUPPORTED,
            seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id, 0U, 0U,
            breadcrumb_path);
        return sl_http_dispatch_unsupported_method(arena, out_diag);
    }

    if (request->path.length == 0U || request->path.ptr == NULL || request->path.ptr[0] != '/') {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    {
        uint64_t started_ns = sl_http_profile_now_ns();
        status = sl_http_dispatch_find_route(arena, dispatch_table, request, &binding,
                                             &method_mismatch, NULL, NULL);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_ROUTE_DISPATCH,
                                     sl_http_profile_now_ns() - started_ns);
    }
    if (!sl_status_is_ok(status)) {
        if (sl_status_code(status) == SL_STATUS_INTERNAL) {
            return sl_http_dispatch_write_diag(
                arena, out_diag, SL_DIAG_ROUTE_VALIDATE_MISMATCH,
                sl_http_dispatch_literal("compiled and classic route dispatch disagreed",
                                         sizeof("compiled and classic route dispatch disagreed") -
                                             1U),
                sl_http_dispatch_literal("rebuild route artifacts or force classic dispatch for "
                                         "triage",
                                         sizeof("rebuild route artifacts or force classic dispatch "
                                                "for triage") -
                                             1U),
                SL_STATUS_INTERNAL);
        }
        return status;
    }

    if (binding == NULL) {
        if (method_mismatch) {
            sl_http_dispatch_record_breadcrumb(
                SL_DIAG_SUBSYSTEM_HTTP, SL_BREADCRUMB_EVENT_METHOD_MISMATCH, SL_STATUS_UNSUPPORTED,
                seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id, 0U,
                0U, breadcrumb_path);
            return sl_http_dispatch_method_not_allowed(arena, out_diag);
        }
        sl_http_dispatch_record_breadcrumb(
            SL_DIAG_SUBSYSTEM_HTTP, SL_BREADCRUMB_EVENT_ROUTE_NOT_FOUND, SL_STATUS_OUT_OF_RANGE,
            seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id, 0U, 0U,
            breadcrumb_path);
        return sl_http_dispatch_missing_route(arena, out_diag);
    }
    sl_http_dispatch_record_breadcrumb(
        SL_DIAG_SUBSYSTEM_HTTP, SL_BREADCRUMB_EVENT_ROUTE_MATCHED, SL_STATUS_OK,
        seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id,
        (uint64_t)binding->route_index, (uint64_t)binding->handler_id,
        binding->pattern == NULL ? sl_str_empty() : binding->pattern->source);
    sl_http_profile_count(dispatch_table->dispatch_mode == SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC
                              ? SL_HTTP_PROFILE_COUNTER_CLASSIC_ROUTE_HITS
                              : SL_HTTP_PROFILE_COUNTER_NATIVE_ROUTE_HITS,
                          1U);

    if (binding->route_index < plan->route_count &&
        sl_plan_route_is_websocket(&plan->routes[binding->route_index]))
    {
        return sl_http_dispatch_websocket_requires_upgrade(arena, out_diag);
    }

    if (dispatch_table->handler_cache_trusted && dispatch_table->plan == plan &&
        binding->handler != NULL && binding->handler->id == binding->handler_id &&
        !sl_str_is_empty(binding->handler->export_name))
    {
        handler = binding->handler;
        use_cached_handler = true;
    }
    else {
        status = sl_plan_find_handler_by_id(plan, binding->handler_id, &handler);
        if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
            return sl_http_dispatch_missing_handler(arena, out_diag);
        }

        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    validation_route = sl_http_dispatch_find_validation_route(plan, binding);
    if (validation_route != NULL && sl_http_dispatch_route_has_native_response(validation_route) &&
        !sl_plan_route_has_bindings(validation_route))
    {
        needs = (SlHttpDispatchContextNeeds){0};
    }
    else {
        needs = sl_http_dispatch_context_needs(validation_route);
    }
    json_mode = sl_http_dispatch_json_mode();
    use_native_json_validation =
        sl_http_dispatch_route_uses_native_json_validation(validation_route) &&
        json_mode != SL_HTTP_DISPATCH_JSON_GENERIC;
    if (use_native_json_validation) {
        sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_NATIVE_JSON_HITS, 1U);
    }
    else if (sl_http_dispatch_route_expects_json_body(validation_route)) {
        sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_GENERIC_JSON_HITS, 1U);
    }

    {
        uint64_t started_ns = sl_http_profile_now_ns();
        status = sl_http_dispatch_apply_body_policy(
            arena, request, sl_http_dispatch_effective_max_body_length(validation_route, seed),
            sl_http_dispatch_route_expects_json_body(validation_route), use_native_json_validation,
            &request_context.body_kind, out_diag);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_BODY_POLICY,
                                     sl_http_profile_now_ns() - started_ns);
    }
    if (!sl_status_is_ok(status)) {
        if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) {
            sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_JSON_REJECTS, 1U);
        }
        return status;
    }

    if (needs.route_params && !route_match_captured) {
        uint64_t started_ns = sl_http_profile_now_ns();
        status = sl_http_dispatch_capture_route_params(arena, binding, request, &route_match);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_BODY_MATERIALIZATION,
                                     sl_http_profile_now_ns() - started_ns);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_MATERIALIZATIONS, 1U);
    }
    else {
        route_match.matched = true;
    }

    if (needs.query_params) {
        uint64_t started_ns = sl_http_profile_now_ns();
        status = sl_http_query_parse(arena, request->raw_target, &query);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_BODY_MATERIALIZATION,
                                     sl_http_profile_now_ns() - started_ns);
        if (!sl_status_is_ok(status)) {
            return sl_http_dispatch_write_diag(
                arena, out_diag, SL_DIAG_INVALID_HTTP_REQUEST,
                sl_http_dispatch_literal("HTTP query string is malformed",
                                         sizeof("HTTP query string is malformed") - 1U),
                sl_http_dispatch_literal(
                    "Percent escapes in query strings must use two hex digits.",
                    sizeof("Percent escapes in query strings must use two hex digits.") - 1U),
                SL_STATUS_INVALID_ARGUMENT);
        }
        sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_MATERIALIZATIONS, 1U);
    }

    request_context.request = request;
    sl_http_dispatch_apply_metadata(request, seed, &request_context);
    request_context.route_params = needs.route_params ? route_match.params : NULL;
    request_context.route_param_count = needs.route_params ? route_match.param_count : 0U;
    request_context.query_params = needs.query_params ? query.params : NULL;
    request_context.query_param_count = needs.query_params ? query.param_count : 0U;
    request_context.needs_route_params = needs.route_params;
    request_context.needs_query_params = needs.query_params;
    request_context.needs_headers = needs.headers;
    request_context.needs_body = needs.body;
    if (needs.body) {
        sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_MATERIALIZATIONS, 1U);
    }
    request_context.needs_header_facade = needs.header_facade;
    request_context.needs_request = needs.request;
    request_context.needs_connection = needs.connection;
    request_context.needs_signal = needs.signal;
    request_context.needs_log = needs.log;
    request_context.needs_metadata = needs.metadata;
    request_context.route_pattern =
        binding->pattern == NULL ? sl_str_empty() : binding->pattern->source;
    request_context.route_name = validation_route == NULL ? sl_str_empty() : validation_route->name;
    request_context.native_json_validated = false;
    if (validation_route != NULL &&
        validation_route->json_request.mode == SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA)
    {
        request_context.request_schema =
            sl_http_dispatch_find_schema(plan, validation_route->json_request.schema);
    }
    if (validation_route != NULL &&
        validation_route->json_response.mode == SL_PLAN_JSON_RESPONSE_NATIVE_SCHEMA &&
        validation_route->json_response.writer == SL_PLAN_JSON_WRITER_BOUNDED)
    {
        request_context.response_schema =
            sl_http_dispatch_find_schema(plan, validation_route->json_response.schema);
    }
    if (validation_route != NULL) {
        uint64_t started_ns = sl_http_profile_now_ns();
        status = sl_request_validation_validate(arena, plan, validation_route, &request_context,
                                                out_result, out_diag);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_JSON_VALIDATION,
                                     sl_http_profile_now_ns() - started_ns);
        if (!sl_status_is_ok(status) || out_result->kind != SL_ENGINE_RESULT_NONE) {
            if (!sl_status_is_ok(status)) {
                sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_JSON_REJECTS, 1U);
            }
            return status;
        }
        request_context.native_json_validated = use_native_json_validation;
        if (request_context.native_json_validated && needs.body) {
            sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_DUPLICATE_VALIDATION_SKIPPED, 1U);
        }
        if (sl_http_dispatch_route_has_native_response(validation_route)) {
            sl_http_dispatch_record_breadcrumb(
                SL_DIAG_SUBSYSTEM_HTTP, SL_BREADCRUMB_EVENT_NATIVE_RESPONSE_HIT, SL_STATUS_OK,
                seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id,
                (uint64_t)binding->route_index, (uint64_t)binding->handler_id,
                validation_route->pattern);
            sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_NATIVE_RESPONSE_ELIGIBLE, 1U);
            sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_NATIVE_RESPONSE_HITS, 1U);
            started_ns = sl_http_profile_now_ns();
            status = sl_http_dispatch_native_response(validation_route, out_result);
            sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_NATIVE_RESPONSE_SELECTION,
                                         sl_http_profile_now_ns() - started_ns);
            return status;
        }
    }

    sl_http_dispatch_record_breadcrumb(
        SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_ENTER, SL_STATUS_OK,
        seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id,
        (uint64_t)binding->route_index, (uint64_t)binding->handler_id,
        handler != NULL ? handler->export_name : sl_str_empty());
    if (use_cached_handler) {
        uint64_t started_ns = sl_http_profile_now_ns();
        sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_V8_HANDLER_CALLS, 1U);
        status = sl_engine_call_registered_handler_with_context(
            engine, arena, binding->handler_id, &request_context, out_result, out_diag);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_HANDLER_EXECUTION,
                                     sl_http_profile_now_ns() - started_ns);
        if (sl_status_is_ok(status)) {
            sl_http_dispatch_materialize_text_result(out_result);
            sl_http_dispatch_record_breadcrumb(
                SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_EXIT, SL_STATUS_OK,
                seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id,
                (uint64_t)binding->route_index, (uint64_t)binding->handler_id, sl_str_empty());
        }
        else {
            sl_http_dispatch_record_breadcrumb(
                SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_EXCEPTION,
                sl_status_code(status), seed == NULL ? 0U : seed->request_id,
                seed == NULL ? 0U : seed->connection_id, (uint64_t)binding->route_index,
                (uint64_t)binding->handler_id,
                out_diag == NULL ? sl_str_empty() : out_diag->message);
        }
        return status;
    }

    {
        uint64_t started_ns = sl_http_profile_now_ns();
        sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_V8_HANDLER_CALLS, 1U);
        status = sl_runtime_contract_call_handler_with_context(
            engine, arena, plan, binding->handler_id, &request_context, out_result, out_diag);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_HANDLER_EXECUTION,
                                     sl_http_profile_now_ns() - started_ns);
    }
    if (sl_status_is_ok(status)) {
        sl_http_dispatch_materialize_text_result(out_result);
        sl_http_dispatch_record_breadcrumb(
            SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_EXIT, SL_STATUS_OK,
            seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id,
            (uint64_t)binding->route_index, (uint64_t)binding->handler_id, sl_str_empty());
    }
    else {
        sl_http_dispatch_record_breadcrumb(
            SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_EXCEPTION, sl_status_code(status),
            seed == NULL ? 0U : seed->request_id, seed == NULL ? 0U : seed->connection_id,
            (uint64_t)binding->route_index, (uint64_t)binding->handler_id,
            out_diag == NULL ? sl_str_empty() : out_diag->message);
    }
    return status;
}

static SlStr sl_http_dispatch_metrics_mode_name(SlHttpRouteDispatchMode mode)
{
    if (mode == SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC) {
        return sl_str_from_cstr("classic");
    }
    if (mode == SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE) {
        return sl_str_from_cstr("validate");
    }
    return sl_str_from_cstr("compiled");
}

static SlStr sl_http_dispatch_metrics_status_class(uint16_t status)
{
    if (status >= 500U) {
        return sl_str_from_cstr("5xx");
    }
    if (status >= 400U) {
        return sl_str_from_cstr("4xx");
    }
    if (status >= 300U) {
        return sl_str_from_cstr("3xx");
    }
    if (status >= 200U) {
        return sl_str_from_cstr("2xx");
    }
    return sl_str_from_cstr("1xx");
}

static uint16_t sl_http_dispatch_metrics_status_from_result(SlStatus status,
                                                            const SlEngineResult* result)
{
    if (sl_status_is_ok(status)) {
        if (result != NULL && result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE) {
            return result->response.status;
        }
        if (result != NULL && result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_TEXT) {
            return 200U;
        }
        return 204U;
    }
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        return 404U;
    }
    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED) {
        return 405U;
    }
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED) {
        return 413U;
    }
    if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) {
        return 400U;
    }
    if (sl_status_code(status) == SL_STATUS_DEADLINE_EXCEEDED) {
        return 503U;
    }
    return 500U;
}

static void sl_http_dispatch_ignore_metric_status(SlStatus status)
{
    if (!sl_status_is_ok(status)) {
        return;
    }
}

static void sl_http_dispatch_record_metrics(SlOpsMetricsRegistry* metrics, SlStr route_pattern,
                                            SlHttpRouteDispatchMode mode, SlStatus status,
                                            const SlHttpRequestHead* request,
                                            const SlEngineResult* result, uint64_t elapsed_ns,
                                            bool validate_mismatch, bool native_json_validation,
                                            bool generic_json_request)
{
    char status_code_buffer[SL_STRING_FORMAT_U64_CAPACITY];
    SlStr status_code = sl_str_empty();
    uint16_t http_status = sl_http_dispatch_metrics_status_from_result(status, result);
    SlOpsMetricLabel route_labels[2];
    SlOpsMetricLabel status_labels[3];
    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    static const double duration_ms_buckets[] = {1.0,   5.0,   10.0,   25.0,   50.0,  100.0,
                                                 250.0, 500.0, 1000.0, 2500.0, 5000.0};
    static const double dispatch_ns_buckets[] = {1000.0,    5000.0,    10000.0,  25000.0,
                                                 50000.0,   100000.0,  250000.0, 500000.0,
                                                 1000000.0, 2500000.0, 5000000.0};

    if (metrics == NULL) {
        return;
    }

    if (sl_str_is_empty(route_pattern)) {
        route_pattern = sl_str_from_cstr("unmatched");
    }
    route_labels[0] = (SlOpsMetricLabel){sl_str_from_cstr("route"), route_pattern};
    route_labels[1] =
        (SlOpsMetricLabel){sl_str_from_cstr("dispatch"), sl_http_dispatch_metrics_mode_name(mode)};
    sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
        metrics, sl_str_from_cstr("http.requests.total"), route_labels, 2U, 1.0));
    sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
        metrics, sl_str_from_cstr("http.route.hits"), route_labels, 2U, 1.0));
    sl_http_dispatch_ignore_metric_status(sl_ops_metrics_histogram_observe(
        metrics, sl_str_from_cstr("http.request.duration.ms"), route_labels, 2U,
        duration_ms_buckets, sizeof(duration_ms_buckets) / sizeof(duration_ms_buckets[0]),
        elapsed_ms));
    sl_http_dispatch_ignore_metric_status(sl_ops_metrics_histogram_observe(
        metrics, sl_str_from_cstr("routing.dispatch.duration.ns"), route_labels, 2U,
        dispatch_ns_buckets, sizeof(dispatch_ns_buckets) / sizeof(dispatch_ns_buckets[0]),
        (double)elapsed_ns));
    if (request != NULL) {
        sl_http_dispatch_ignore_metric_status(
            sl_ops_metrics_counter_inc(metrics, sl_str_from_cstr("http.request.bytes"),
                                       route_labels, 2U, (double)request->body.length));
    }
    if (result != NULL && result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE) {
        sl_http_dispatch_ignore_metric_status(
            sl_ops_metrics_counter_inc(metrics, sl_str_from_cstr("http.response.bytes"),
                                       route_labels, 2U, (double)result->response.body.length));
    }
    if (!sl_status_is_ok(sl_string_format_u64(status_code_buffer, sizeof(status_code_buffer),
                                              http_status, &status_code)))
    {
        status_code = sl_str_from_cstr("0");
    }
    status_labels[0] = route_labels[0];
    status_labels[1] = (SlOpsMetricLabel){sl_str_from_cstr("status"), status_code};
    status_labels[2] = (SlOpsMetricLabel){sl_str_from_cstr("class"),
                                          sl_http_dispatch_metrics_status_class(http_status)};
    sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
        metrics, sl_str_from_cstr("http.status.total"), status_labels, 3U, 1.0));
    if (http_status >= 500U) {
        sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
            metrics, sl_str_from_cstr("http.errors.total"), route_labels, 2U, 1.0));
    }
    if (mode == SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC) {
        sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
            metrics, sl_str_from_cstr("routing.classic.hits"), route_labels, 2U, 1.0));
    }
    else {
        sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
            metrics, sl_str_from_cstr("routing.compiled.hits"), route_labels, 2U, 1.0));
    }
    if (mode == SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE && validate_mismatch) {
        sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
            metrics, sl_str_from_cstr("routing.validate.mismatches"), route_labels, 2U, 1.0));
    }
    if (native_json_validation) {
        sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
            metrics, sl_str_from_cstr("json.native.request.hits"), route_labels, 2U, 1.0));
    }
    else if (generic_json_request) {
        sl_http_dispatch_ignore_metric_status(sl_ops_metrics_counter_inc(
            metrics, sl_str_from_cstr("json.generic.request.hits"), route_labels, 2U, 1.0));
    }
}

static SlStatus sl_http_dispatch_request_recorded(SlArena* arena, SlEngine* engine,
                                                  const SlPlan* plan,
                                                  const SlHttpDispatchTable* dispatch_table,
                                                  const SlHttpRequestHead* request,
                                                  const SlHttpDispatchContextSeed* seed,
                                                  SlEngineResult* out_result, SlDiag* out_diag)
{
    SlOpsMetricsRegistry* metrics = dispatch_table == NULL ? NULL : dispatch_table->metrics;
    const SlHttpRouteBinding* binding = NULL;
    const SlPlanRoute* validation_route = NULL;
    bool method_mismatch = false;
    SlStr route_pattern = sl_str_from_cstr("unmatched");
    uint64_t start_ns = 0U;
    uint64_t end_ns = 0U;
    SlStatus status;
    SlStatus metric_status;
    bool native_json_validation = false;
    bool generic_json_request = false;
    bool validate_mismatch = false;

    if (metrics == NULL) {
        return sl_http_dispatch_request_core(arena, engine, plan, dispatch_table, request, seed,
                                             out_result, out_diag);
    }

    sl_http_dispatch_ignore_metric_status(sl_platform_monotonic_time_ns(&start_ns));
    sl_http_dispatch_ignore_metric_status(
        sl_ops_metrics_gauge_add(metrics, sl_str_from_cstr("http.requests.active"), NULL, 0U, 1.0));
    if (request != NULL && sl_http_dispatch_request_method_runnable(request->method) &&
        request->path.length != 0U && request->path.ptr != NULL && request->path.ptr[0] == '/')
    {
        metric_status = sl_http_dispatch_find_route(arena, dispatch_table, request, &binding,
                                                    &method_mismatch, NULL, NULL);
        if (sl_status_is_ok(metric_status) && binding != NULL && binding->pattern != NULL) {
            SlHttpDispatchJsonMode json_mode = sl_http_dispatch_json_mode();

            route_pattern = binding->pattern->source;
            validation_route = sl_http_dispatch_find_validation_route(plan, binding);
            native_json_validation =
                sl_http_dispatch_route_uses_native_json_validation(validation_route) &&
                json_mode != SL_HTTP_DISPATCH_JSON_GENERIC;
            generic_json_request = !native_json_validation && request->body.length != 0U &&
                                   sl_http_dispatch_route_expects_json_body(validation_route);
        }
        else if (method_mismatch) {
            route_pattern = sl_str_from_cstr("method_mismatch");
        }
    }

    status = sl_http_dispatch_request_core(arena, engine, plan, dispatch_table, request, seed,
                                           out_result, out_diag);
    validate_mismatch = dispatch_table->dispatch_mode == SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE &&
                        out_diag != NULL && out_diag->code == SL_DIAG_ROUTE_VALIDATE_MISMATCH &&
                        !sl_status_is_ok(status);
    sl_http_dispatch_ignore_metric_status(sl_ops_metrics_gauge_add(
        metrics, sl_str_from_cstr("http.requests.active"), NULL, 0U, -1.0));
    sl_http_dispatch_ignore_metric_status(sl_platform_monotonic_time_ns(&end_ns));
    sl_http_dispatch_record_metrics(metrics, route_pattern, dispatch_table->dispatch_mode, status,
                                    request, out_result,
                                    end_ns >= start_ns ? end_ns - start_ns : 0U, validate_mismatch,
                                    native_json_validation, generic_json_request);
    return status;
}

SlStatus sl_http_dispatch_request_head(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                       const SlHttpDispatchTable* dispatch_table,
                                       const SlHttpRequestHead* request, SlEngineResult* out_result,
                                       SlDiag* out_diag)
{
    return sl_http_dispatch_request_recorded(arena, engine, plan, dispatch_table, request, NULL,
                                             out_result, out_diag);
}

SlStatus sl_http_dispatch_request_lifecycle(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                            const SlHttpDispatchTable* dispatch_table,
                                            const SlHttpRequestLifecycle* request,
                                            SlEngineResult* out_result, SlDiag* out_diag)
{
    SlHttpDispatchContextSeed seed = {0};

    if (request == NULL) {
        return sl_http_dispatch_request_recorded(arena, engine, plan, dispatch_table, NULL, NULL,
                                                 out_result, out_diag);
    }

    seed.request_id = request->id;
    seed.connection_id = request->connection == NULL ? 0U : request->connection->id;
    seed.scheme = request->scheme.ptr == NULL || sl_str_is_empty(request->scheme)
                      ? sl_str_from_cstr("http")
                      : request->scheme;
    seed.max_body_length = request->connection == NULL || request->connection->backend == NULL
                               ? SL_HTTP_DEFAULT_MAX_BODY_LENGTH
                               : request->connection->backend->options.parse.max_body_length;
    seed.cancellation = &request->cancellation;
    return sl_http_dispatch_request_recorded(arena, engine, plan, dispatch_table, &request->head,
                                             &seed, out_result, out_diag);
}
