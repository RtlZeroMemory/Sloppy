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

#include "sloppy/http_context.h"
#include "sloppy/request_validation.h"
#include "sloppy/runtime_contract.h"

#include "sloppy/builder.h"
#include "sloppy/container.h"

#include <yyjson.h>

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

static bool sl_http_dispatch_power_of_two(size_t value);

static SlStatus sl_http_dispatch_validate_table(const SlHttpDispatchTable* dispatch_table)
{
    size_t index = 0U;

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
         !sl_http_dispatch_power_of_two(dispatch_table->param_route_bucket_slot_count)))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (dispatch_table->handler_cache_trusted) {
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

    for (index = 0U; index < dispatch_table->param_route_bucket_count; index += 1U) {
        const SlHttpRouteCandidateBucket* bucket = &dispatch_table->param_route_buckets[index];
        if (bucket->route_count != 0U && bucket->routes == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
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
        arena, out_diag, SL_DIAG_HTTP_UNSUPPORTED_METHOD,
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
        if (sl_str_equal_ci_ascii(header->name, sl_str_from_cstr("Transfer-Encoding"))) {
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

    if (!sl_http_dispatch_find_header(request, sl_str_from_cstr("Transfer-Encoding"), &value)) {
        return false;
    }
    return sl_str_equal_ci_ascii(sl_http_dispatch_trim_ascii_space(value),
                                 sl_str_from_cstr("chunked"));
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
        return sl_str_from_cstr("HTTP/1.0");
    }
    if (request->version_minor == 1U) {
        return sl_str_from_cstr("HTTP/1.1");
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
            ? sl_str_from_cstr("http")
            : seed->scheme;
    request_context->protocol = sl_http_dispatch_protocol_name(request);
    request_context->query_string = sl_http_dispatch_extract_query_string(request->raw_target);
    if (seed != NULL) {
        request_context->request_id = seed->request_id;
        request_context->connection_id = seed->connection_id;
        request_context->cancellation = seed->cancellation;
    }
    if (sl_http_dispatch_find_header(request, sl_str_from_cstr("Content-Type"), &content_type)) {
        request_context->content_type = sl_http_dispatch_trim_ascii_space(content_type);
    }
    if (sl_http_dispatch_find_header(request, sl_str_from_cstr("Content-Length"),
                                     &content_length) &&
        sl_http_dispatch_parse_size_decimal(sl_http_dispatch_trim_ascii_space(content_length),
                                            &parsed_content_length))
    {
        request_context->has_content_length = true;
        request_context->content_length = (uint64_t)parsed_content_length;
    }
}

static bool sl_http_dispatch_media_type_json(SlStr media_type)
{
    return sl_str_equal_ci_ascii(media_type, sl_str_from_cstr("application/json")) ||
           (sl_str_starts_with_ci_ascii(media_type, sl_str_from_cstr("application/")) &&
            sl_str_ends_with_ci_ascii(media_type, sl_str_from_cstr("+json")));
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
                                                   size_t max_body_length,
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
        if (sl_http_dispatch_find_header(request, sl_str_from_cstr("Content-Type"), &content_type))
        {
            media_type = sl_http_dispatch_media_type(content_type);
            if (sl_str_equal_ci_ascii(media_type,
                                      sl_str_from_cstr("application/x-www-form-urlencoded")))
            {
                *out_body_kind = SL_HTTP_REQUEST_BODY_FORM;
            }
            else if (sl_str_equal_ci_ascii(media_type, sl_str_from_cstr("multipart/form-data"))) {
                *out_body_kind = SL_HTTP_REQUEST_BODY_MULTIPART;
            }
        }
        return sl_status_ok();
    }
    if (request->body.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_http_dispatch_request_transfer_encoding_chunked(request) &&
        (!sl_http_dispatch_find_header(request, sl_str_from_cstr("Content-Length"),
                                       &content_length) ||
         sl_str_is_empty(sl_http_dispatch_trim_ascii_space(content_length))))
    {
        return sl_http_dispatch_unsupported_body(arena, out_diag);
    }
    if (request->body.length > max_body_length) {
        return sl_http_dispatch_body_too_large(arena, out_diag);
    }

    if (!sl_http_dispatch_find_header(request, sl_str_from_cstr("Content-Type"), &content_type)) {
        return sl_http_dispatch_unsupported_media_type(arena, out_diag);
    }

    media_type = sl_http_dispatch_media_type(content_type);
    if (sl_http_dispatch_media_type_json(media_type)) {
        SlStatus status = sl_http_dispatch_validate_json_body(arena, request->body, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_body_kind = SL_HTTP_REQUEST_BODY_JSON;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, sl_str_from_cstr("text/plain"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_TEXT;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, sl_str_from_cstr("application/octet-stream"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_BYTES;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, sl_str_from_cstr("application/x-www-form-urlencoded"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_FORM;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(media_type, sl_str_from_cstr("multipart/form-data"))) {
        *out_body_kind = SL_HTTP_REQUEST_BODY_MULTIPART;
        return sl_status_ok();
    }

    return sl_http_dispatch_unsupported_media_type(arena, out_diag);
}

static bool sl_http_route_entry_less(const SlHttpRouteTableEntry* left,
                                     const SlHttpRouteTableEntry* right)
{
    if (left->has_params != right->has_params) {
        return !left->has_params;
    }

    if (left->static_segment_count != right->static_segment_count) {
        return left->static_segment_count > right->static_segment_count;
    }

    if (left->constrained_param_count != right->constrained_param_count) {
        return left->constrained_param_count > right->constrained_param_count;
    }

    if (left->pattern.segment_count != right->pattern.segment_count) {
        return left->pattern.segment_count > right->pattern.segment_count;
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
    out_table->dispatch.plan = plan;
    out_table->dispatch.handler_cache_trusted = true;
    status = sl_http_route_table_build_exact_index(arena, bindings, runnable_route_count,
                                                   &out_table->dispatch);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
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
    if (first_route->route_index <= generic_route->route_index) {
        *first_index += 1U;
        return first_route;
    }

    *generic_index += 1U;
    return generic_route;
}

static bool sl_http_dispatch_binding_valid(const SlHttpRouteBinding* binding)
{
    return binding != NULL && sl_http_dispatch_binding_method_runnable(binding->method) &&
           binding->pattern != NULL && sl_handler_id_valid(binding->handler_id);
}

static SlStatus
sl_http_dispatch_find_param_route_for_method(const SlHttpDispatchTable* dispatch_table,
                                             const SlHttpRequestHead* request, SlHttpMethod method,
                                             const SlHttpRouteBinding** out_binding)
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
    status =
        sl_http_dispatch_find_param_route_for_method(dispatch_table, request, method, &binding);
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

    if (out_has_other_method == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_has_other_method = false;
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

static SlStatus sl_http_dispatch_find_route(SlArena* arena,
                                            const SlHttpDispatchTable* dispatch_table,
                                            const SlHttpRequestHead* request,
                                            const SlHttpRouteBinding** out_binding,
                                            bool* out_method_mismatch)
{
    SlHttpMethod match_method = SL_HTTP_METHOD_UNKNOWN;

    if (arena == NULL || dispatch_table == NULL || request == NULL || out_binding == NULL ||
        out_method_mismatch == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    (void)arena;
    *out_binding = NULL;
    *out_method_mismatch = false;
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
        SlStatus status = sl_http_dispatch_find_param_route_for_method(dispatch_table, request,
                                                                       match_method, out_binding);
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
        if (!sl_http_dispatch_pattern_could_match(binding->pattern, request->path)) {
            continue;
        }
        if (binding->method != match_method) {
            *out_method_mismatch = true;
            continue;
        }
        *out_binding = binding;
        return sl_status_ok();
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
    bool method_mismatch = false;
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

    status =
        sl_http_dispatch_find_route(arena, dispatch_table, request, &binding, &method_mismatch);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (binding == NULL) {
        if (method_mismatch) {
            return sl_http_dispatch_method_not_allowed(arena, out_diag);
        }
        return sl_http_dispatch_missing_route(arena, out_diag);
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
    needs = sl_http_dispatch_context_needs(validation_route);

    status = sl_http_dispatch_apply_body_policy(arena, request,
                                                seed == NULL || seed->max_body_length == 0U
                                                    ? SL_HTTP_DEFAULT_MAX_BODY_LENGTH
                                                    : seed->max_body_length,
                                                &request_context.body_kind, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (needs.route_params) {
        status = sl_http_dispatch_capture_route_params(arena, binding, request, &route_match);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    else {
        route_match.matched = true;
    }

    if (needs.query_params) {
        status = sl_http_query_parse(arena, request->raw_target, &query);
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
    request_context.needs_header_facade = needs.header_facade;
    request_context.needs_request = needs.request;
    request_context.needs_connection = needs.connection;
    request_context.needs_signal = needs.signal;
    request_context.needs_log = needs.log;
    request_context.needs_metadata = needs.metadata;
    request_context.route_pattern =
        binding->pattern == NULL ? sl_str_empty() : binding->pattern->source;
    request_context.route_name = validation_route == NULL ? sl_str_empty() : validation_route->name;
    if (validation_route != NULL) {
        status = sl_request_validation_validate(arena, plan, validation_route, &request_context,
                                                out_result, out_diag);
        if (!sl_status_is_ok(status) || out_result->kind != SL_ENGINE_RESULT_NONE) {
            return status;
        }
    }

    if (use_cached_handler) {
        return sl_engine_call_registered_handler_with_context(
            engine, arena, binding->handler_id, &request_context, out_result, out_diag);
    }

    return sl_runtime_contract_call_handler_with_context(engine, arena, plan, binding->handler_id,
                                                         &request_context, out_result, out_diag);
}

SlStatus sl_http_dispatch_request_head(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                       const SlHttpDispatchTable* dispatch_table,
                                       const SlHttpRequestHead* request, SlEngineResult* out_result,
                                       SlDiag* out_diag)
{
    return sl_http_dispatch_request_core(arena, engine, plan, dispatch_table, request, NULL,
                                         out_result, out_diag);
}

SlStatus sl_http_dispatch_request_lifecycle(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                            const SlHttpDispatchTable* dispatch_table,
                                            const SlHttpRequestLifecycle* request,
                                            SlEngineResult* out_result, SlDiag* out_diag)
{
    SlHttpDispatchContextSeed seed = {0};

    if (request == NULL) {
        return sl_http_dispatch_request_core(arena, engine, plan, dispatch_table, NULL, NULL,
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
    return sl_http_dispatch_request_core(arena, engine, plan, dispatch_table, &request->head, &seed,
                                         out_result, out_diag);
}
