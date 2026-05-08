/*
 * src/core/http.c
 *
 * Implements the bounded complete-buffer HTTP parser. llhttp parses a complete in-memory
 * request head into Sloppy-owned request-head fields; socket/event-loop transport lives
 * behind the platform HTTP transport boundary.
 *
 * Safety invariants:
 * - parsed request-head strings and headers are copied into the caller-provided arena;
 * - complete-buffer body bytes are copied into the caller-provided arena and bounded by
 *   caller options;
 * - parser failure resets arena allocations made by this call before building diagnostics;
 * - header count is bounded by caller options or SL_HTTP_DEFAULT_MAX_HEADERS;
 * - this module creates no sockets, server, streaming parser API, SlLoop/libuv bridge, V8
 *   dependency, route dispatch, body content parser, query parser, or percent decoder.
 *
 * Tests: tests/unit/core/test_http.c.
 */
#include "sloppy/http.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"
#include "sloppy/container.h"

#include <llhttp.h>
#include <stdint.h>

typedef struct SlHttpParseContext
{
    SlArena* arena;
    SlHttpHeader* headers;
    size_t max_headers;
    size_t max_request_line_length;
    size_t max_target_length;
    size_t max_header_name_length;
    size_t max_header_value_length;
    size_t max_total_header_bytes;
    size_t max_body_length;
    size_t header_count;
    size_t total_header_bytes;
    SlStringBuilder raw_target_builder;
    SlStringBuilder current_header_name_builder;
    SlStringBuilder current_header_value_builder;
    SlByteBuilder body_builder;
    SlStr raw_target;
    SlBytes body;
    SlStr current_header_name;
    SlStr current_header_value;
    bool saw_header_value;
    bool headers_complete;
    bool request_complete;
    SlStatus callback_status;
    SlDiagCode diag_code;
    SlStr diag_message;
    SlStr diag_hint;
} SlHttpParseContext;

static SlStr sl_http_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlStatusCode sl_http_status_code_for_diag(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_HTTP_HEADER_LIMIT:
    case SL_DIAG_HTTP_TARGET_LIMIT:
    case SL_DIAG_HTTP_HEADER_NAME_LIMIT:
    case SL_DIAG_HTTP_HEADER_VALUE_LIMIT:
    case SL_DIAG_HTTP_HEADER_BYTES_LIMIT:
    case SL_DIAG_HTTP_REQUEST_LINE_LIMIT:
    case SL_DIAG_HTTP_BODY_LIMIT:
        return SL_STATUS_CAPACITY_EXCEEDED;
    default:
        return SL_STATUS_INVALID_ARGUMENT;
    }
}

static bool sl_http_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static SlStr sl_http_trim_ows(SlStr value)
{
    size_t start = 0U;
    size_t end = value.length;

    if (value.ptr == NULL && value.length != 0U) {
        return sl_str_empty();
    }

    while (start < end && (value.ptr[start] == ' ' || value.ptr[start] == '\t')) {
        start += 1U;
    }
    while (end > start && (value.ptr[end - 1U] == ' ' || value.ptr[end - 1U] == '\t')) {
        end -= 1U;
    }

    return sl_str_from_parts(value.ptr == NULL ? NULL : value.ptr + start, end - start);
}

static bool sl_http_header_name_equal(SlStr name, const char* expected)
{
    return sl_str_equal_ci_ascii(name, sl_str_from_cstr(expected));
}

static SlStatus sl_http_set_error(SlHttpParseContext* ctx, SlDiagCode code, SlStr message,
                                  SlStr hint)
{
    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    ctx->diag_code = code;
    ctx->diag_message = message;
    ctx->diag_hint = hint;
    return sl_status_from_code(sl_http_status_code_for_diag(code));
}

bool sl_http_method_supported(SlHttpMethod method)
{
    switch (method) {
    case SL_HTTP_METHOD_GET:
    case SL_HTTP_METHOD_POST:
    case SL_HTTP_METHOD_PUT:
    case SL_HTTP_METHOD_PATCH:
    case SL_HTTP_METHOD_DELETE:
        return true;
    case SL_HTTP_METHOD_UNKNOWN:
    case SL_HTTP_METHOD_OPTIONS:
    case SL_HTTP_METHOD_HEAD:
    default:
        return false;
    }
}

SlStatus sl_http_method_from_str(SlStr method, SlHttpMethod* out_method)
{
    if (out_method == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_method = SL_HTTP_METHOD_UNKNOWN;
    if (sl_str_equal(method, sl_str_from_cstr("GET"))) {
        *out_method = SL_HTTP_METHOD_GET;
        return sl_status_ok();
    }
    if (sl_str_equal(method, sl_str_from_cstr("POST"))) {
        *out_method = SL_HTTP_METHOD_POST;
        return sl_status_ok();
    }
    if (sl_str_equal(method, sl_str_from_cstr("PUT"))) {
        *out_method = SL_HTTP_METHOD_PUT;
        return sl_status_ok();
    }
    if (sl_str_equal(method, sl_str_from_cstr("PATCH"))) {
        *out_method = SL_HTTP_METHOD_PATCH;
        return sl_status_ok();
    }
    if (sl_str_equal(method, sl_str_from_cstr("DELETE"))) {
        *out_method = SL_HTTP_METHOD_DELETE;
        return sl_status_ok();
    }
    if (sl_str_equal(method, sl_str_from_cstr("OPTIONS"))) {
        *out_method = SL_HTTP_METHOD_OPTIONS;
        return sl_status_ok();
    }
    if (sl_str_equal(method, sl_str_from_cstr("HEAD"))) {
        *out_method = SL_HTTP_METHOD_HEAD;
        return sl_status_ok();
    }

    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_copy_str_view(SlArena* arena, SlStr src, SlStr* out)
{
    if (arena == NULL || out == NULL || !sl_http_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_str_copy_view_to_arena(arena, src, out);
}

static SlStatus sl_http_init_parse_builders(SlHttpParseContext* ctx)
{
    SlStatus status;

    if (ctx == NULL || ctx->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_string_builder_init_arena(&ctx->raw_target_builder, ctx->arena, 0U,
                                          ctx->max_target_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_init_arena(&ctx->current_header_name_builder, ctx->arena, 0U,
                                          ctx->max_header_name_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_init_arena(&ctx->current_header_value_builder, ctx->arena, 0U,
                                          ctx->max_header_value_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_byte_builder_init_arena(&ctx->body_builder, ctx->arena, 0U, ctx->max_body_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_ok();
}

static SlStatus sl_http_append_body(SlHttpParseContext* ctx, SlBytes chunk)
{
    size_t next_length = 0U;
    SlStatus status;

    if (ctx == NULL || (chunk.ptr == NULL && chunk.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(ctx->body.length, chunk.length, &next_length);
    if (sl_status_is_ok(status) && next_length > ctx->max_body_length) {
        status = sl_http_set_error(
            ctx, SL_DIAG_HTTP_BODY_LIMIT,
            sl_http_literal("HTTP request body is too large",
                            sizeof("HTTP request body is too large") - 1U),
            sl_http_literal("the dev HTTP runtime rejects bodies above the configured limit",
                            sizeof("the dev HTTP runtime rejects bodies above the configured "
                                   "limit") -
                                1U));
    }
    if (!sl_status_is_ok(status) || chunk.length == 0U) {
        return status;
    }

    status = sl_byte_builder_append_bytes(&ctx->body_builder, chunk);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    ctx->body = sl_byte_builder_view(&ctx->body_builder);
    return sl_status_ok();
}

static SlStatus sl_http_count_header_bytes(SlHttpParseContext* ctx, size_t length)
{
    size_t next_length = 0U;
    SlStatus status;

    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(ctx->total_header_bytes, length, &next_length);
    if (sl_status_is_ok(status) && next_length > ctx->max_total_header_bytes) {
        return sl_http_set_error(
            ctx, SL_DIAG_HTTP_HEADER_BYTES_LIMIT,
            sl_http_literal("HTTP request headers are too large",
                            sizeof("HTTP request headers are too large") - 1U),
            sl_http_literal("the HTTP backend bounds total header bytes before dispatch",
                            sizeof("the HTTP backend bounds total header bytes before dispatch") -
                                1U));
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    ctx->total_header_bytes = next_length;
    return sl_status_ok();
}

static SlStatus sl_http_check_header_name_length(SlHttpParseContext* ctx, size_t length)
{
    size_t next_length = 0U;
    SlStatus status;

    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(ctx->current_header_name.length, length, &next_length);
    if (sl_status_is_ok(status) && next_length > ctx->max_header_name_length) {
        return sl_http_set_error(
            ctx, SL_DIAG_HTTP_HEADER_NAME_LIMIT,
            sl_http_literal("HTTP header name is too long",
                            sizeof("HTTP header name is too long") - 1U),
            sl_http_literal("the HTTP backend bounds header names before dispatch",
                            sizeof("the HTTP backend bounds header names before dispatch") - 1U));
    }

    return status;
}

static SlStatus sl_http_check_header_value_length(SlHttpParseContext* ctx, size_t length)
{
    size_t next_length = 0U;
    SlStatus status;

    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(ctx->current_header_value.length, length, &next_length);
    if (sl_status_is_ok(status) && next_length > ctx->max_header_value_length) {
        return sl_http_set_error(
            ctx, SL_DIAG_HTTP_HEADER_VALUE_LIMIT,
            sl_http_literal("HTTP header value is too long",
                            sizeof("HTTP header value is too long") - 1U),
            sl_http_literal("the HTTP backend bounds header values before dispatch",
                            sizeof("the HTTP backend bounds header values before dispatch") - 1U));
    }

    return status;
}

static SlStatus sl_http_finish_current_header(SlHttpParseContext* ctx)
{
    SlStatus status;

    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (ctx->current_header_name.length == 0U && ctx->current_header_value.length == 0U) {
        return sl_status_ok();
    }

    if (ctx->current_header_name.length == 0U || !ctx->saw_header_value) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("malformed HTTP header", sizeof("malformed HTTP header") - 1U),
            sl_http_literal("headers must have a field name and value separated by colon",
                            sizeof("headers must have a field name and value separated by colon") -
                                1U));
    }

    if (ctx->header_count >= ctx->max_headers) {
        return sl_http_set_error(
            ctx, SL_DIAG_HTTP_HEADER_LIMIT,
            sl_http_literal("HTTP request has too many headers",
                            sizeof("HTTP request has too many headers") - 1U),
            sl_http_literal(
                "increase max_headers only when the caller can bound memory use",
                sizeof("increase max_headers only when the caller can bound memory use") - 1U));
    }

    status = sl_http_copy_str_view(ctx->arena, ctx->current_header_name,
                                   &ctx->headers[ctx->header_count].name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_copy_str_view(ctx->arena, ctx->current_header_value,
                                   &ctx->headers[ctx->header_count].value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    ctx->header_count += 1U;
    ctx->current_header_name = sl_str_empty();
    ctx->current_header_value = sl_str_empty();
    sl_string_builder_reset(&ctx->current_header_name_builder);
    sl_string_builder_reset(&ctx->current_header_value_builder);
    ctx->saw_header_value = false;
    return sl_status_ok();
}

static int sl_http_status_to_llhttp(SlStatus status)
{
    (void)status;
    return HPE_USER;
}

static void sl_http_record_callback_status(SlHttpParseContext* ctx, SlStatus status)
{
    if (ctx != NULL && sl_status_is_ok(ctx->callback_status)) {
        ctx->callback_status = status;
    }
}

static int sl_http_on_url(llhttp_t* parser, const char* at, size_t length)
{
    SlHttpParseContext* ctx = (SlHttpParseContext*)parser->data;
    size_t next_length = 0U;
    SlStatus status = sl_checked_add_size(ctx->raw_target.length, length, &next_length);

    if (sl_status_is_ok(status) && next_length > ctx->max_target_length) {
        status = sl_http_set_error(
            ctx, SL_DIAG_HTTP_TARGET_LIMIT,
            sl_http_literal("HTTP request target is too long",
                            sizeof("HTTP request target is too long") - 1U),
            sl_http_literal("the HTTP backend bounds request targets before dispatch",
                            sizeof("the HTTP backend bounds request targets before dispatch") -
                                1U));
    }
    if (sl_status_is_ok(status)) {
        status =
            sl_string_builder_append_str(&ctx->raw_target_builder, sl_str_from_parts(at, length));
        if (sl_status_is_ok(status)) {
            ctx->raw_target = sl_string_builder_view(&ctx->raw_target_builder);
        }
    }
    if (!sl_status_is_ok(status)) {
        sl_http_record_callback_status(ctx, status);
    }

    return sl_status_is_ok(status) ? HPE_OK : sl_http_status_to_llhttp(status);
}

static int sl_http_on_header_field(llhttp_t* parser, const char* at, size_t length)
{
    SlHttpParseContext* ctx = (SlHttpParseContext*)parser->data;
    SlStatus status;

    if (ctx->saw_header_value) {
        status = sl_http_finish_current_header(ctx);
        if (!sl_status_is_ok(status)) {
            sl_http_record_callback_status(ctx, status);
            return sl_http_status_to_llhttp(status);
        }
    }

    status = sl_http_count_header_bytes(ctx, length);
    if (sl_status_is_ok(status)) {
        status = sl_http_check_header_name_length(ctx, length);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_str(&ctx->current_header_name_builder,
                                              sl_str_from_parts(at, length));
    }
    if (sl_status_is_ok(status)) {
        ctx->current_header_name = sl_string_builder_view(&ctx->current_header_name_builder);
    }
    if (!sl_status_is_ok(status)) {
        sl_http_record_callback_status(ctx, status);
    }

    return sl_status_is_ok(status) ? HPE_OK : sl_http_status_to_llhttp(status);
}

static int sl_http_on_header_value(llhttp_t* parser, const char* at, size_t length)
{
    SlHttpParseContext* ctx = (SlHttpParseContext*)parser->data;
    SlStatus status;

    ctx->saw_header_value = true;
    status = sl_http_count_header_bytes(ctx, length);
    if (sl_status_is_ok(status)) {
        status = sl_http_check_header_value_length(ctx, length);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_str(&ctx->current_header_value_builder,
                                              sl_str_from_parts(at, length));
    }
    if (sl_status_is_ok(status)) {
        ctx->current_header_value = sl_string_builder_view(&ctx->current_header_value_builder);
    }
    if (!sl_status_is_ok(status)) {
        sl_http_record_callback_status(ctx, status);
    }

    return sl_status_is_ok(status) ? HPE_OK : sl_http_status_to_llhttp(status);
}

static int sl_http_on_headers_complete(llhttp_t* parser)
{
    SlHttpParseContext* ctx = (SlHttpParseContext*)parser->data;
    SlStatus status = sl_http_finish_current_header(ctx);

    if (!sl_status_is_ok(status)) {
        sl_http_record_callback_status(ctx, status);
        return sl_http_status_to_llhttp(status);
    }

    ctx->headers_complete = true;
    return HPE_OK;
}

static int sl_http_on_body(llhttp_t* parser, const char* at, size_t length)
{
    SlHttpParseContext* ctx = (SlHttpParseContext*)parser->data;
    SlStatus status =
        sl_http_append_body(ctx, sl_bytes_from_parts((const unsigned char*)at, length));

    if (!sl_status_is_ok(status)) {
        sl_http_record_callback_status(ctx, status);
    }

    return sl_status_is_ok(status) ? HPE_OK : sl_http_status_to_llhttp(status);
}

static int sl_http_on_message_complete(llhttp_t* parser)
{
    SlHttpParseContext* ctx = (SlHttpParseContext*)parser->data;
    ctx->request_complete = true;
    return HPE_OK;
}

static SlHttpMethod sl_http_method_from_llhttp(llhttp_method_t method)
{
    switch (method) {
    case HTTP_GET:
        return SL_HTTP_METHOD_GET;
    case HTTP_POST:
        return SL_HTTP_METHOD_POST;
    case HTTP_PUT:
        return SL_HTTP_METHOD_PUT;
    case HTTP_DELETE:
        return SL_HTTP_METHOD_DELETE;
    case HTTP_PATCH:
        return SL_HTTP_METHOD_PATCH;
    case HTTP_OPTIONS:
        return SL_HTTP_METHOD_OPTIONS;
    case HTTP_HEAD:
        return SL_HTTP_METHOD_HEAD;
    default:
        return SL_HTTP_METHOD_UNKNOWN;
    }
}

static SlStr sl_http_extract_path(SlStr raw_target)
{
    size_t index = 0U;

    for (index = 0U; index < raw_target.length; index += 1U) {
        if (raw_target.ptr[index] == '?') {
            return sl_str_from_parts(raw_target.ptr, index);
        }
    }

    return raw_target;
}

static SlStatus sl_http_build_diag(SlArena* arena, const SlHttpParseContext* ctx, SlDiag* out_diag)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL || ctx == NULL || ctx->diag_code == SL_DIAG_NONE) {
        return sl_status_ok();
    }

    *out_diag = (SlDiag){0};
    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, ctx->diag_code,
                                  ctx->diag_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_str_is_empty(ctx->diag_hint)) {
        status = sl_diag_builder_add_hint(&builder, ctx->diag_hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_diag_builder_finish(&builder, out_diag);
}

static SlStatus sl_http_prepare_headers(SlArena* arena, size_t max_headers,
                                        SlHttpHeader** out_headers)
{
    SlSlice header_slice = {0};
    SlStatus status;

    if (arena == NULL || out_headers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_headers = NULL;
    if (max_headers == 0U) {
        return sl_status_ok();
    }

    status = sl_arena_array_alloc(arena, max_headers, sizeof(SlHttpHeader), _Alignof(SlHttpHeader),
                                  &header_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_headers = (SlHttpHeader*)header_slice.ptr;
    return sl_status_ok();
}

static void sl_http_settings_prepare(llhttp_settings_t* settings)
{
    llhttp_settings_init(settings);
    settings->on_url = sl_http_on_url;
    settings->on_header_field = sl_http_on_header_field;
    settings->on_header_value = sl_http_on_header_value;
    settings->on_headers_complete = sl_http_on_headers_complete;
    settings->on_body = sl_http_on_body;
    settings->on_message_complete = sl_http_on_message_complete;
}

static SlStatus sl_http_check_request_line_length(SlHttpParseContext* ctx, SlBytes bytes)
{
    size_t index = 0U;
    size_t request_line_length = bytes.length;

    if (ctx == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < bytes.length; index += 1U) {
        if (bytes.ptr[index] == '\r' && index + 1U < bytes.length && bytes.ptr[index + 1U] == '\n')
        {
            request_line_length = index;
            break;
        }
    }

    if (request_line_length > ctx->max_request_line_length) {
        return sl_http_set_error(
            ctx, SL_DIAG_HTTP_REQUEST_LINE_LIMIT,
            sl_http_literal("HTTP request line is too long",
                            sizeof("HTTP request line is too long") - 1U),
            sl_http_literal("the HTTP backend bounds method, target, and version line bytes",
                            sizeof("the HTTP backend bounds method, target, and version line "
                                   "bytes") -
                                1U));
    }

    return sl_status_ok();
}

static void sl_http_parse_context_init(SlHttpParseContext* ctx, SlArena* arena,
                                       const SlHttpParseOptions* options)
{
    size_t max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    size_t max_request_line_length = SL_HTTP_DEFAULT_MAX_REQUEST_LINE_LENGTH;
    size_t max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    size_t max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    size_t max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    size_t max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
    size_t max_body_length = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;

    if (options != NULL) {
        /*
         * Zero max_headers is an explicit no-headers limit; the other zero limits use
         * documented defaults so old callers keep the bounded alpha behavior.
         */
        max_headers = options->max_headers;
        max_request_line_length = options->max_request_line_length == 0U
                                      ? SL_HTTP_DEFAULT_MAX_REQUEST_LINE_LENGTH
                                      : options->max_request_line_length;
        max_target_length = options->max_target_length == 0U ? SL_HTTP_DEFAULT_MAX_TARGET_LENGTH
                                                             : options->max_target_length;
        max_header_name_length = options->max_header_name_length == 0U
                                     ? SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH
                                     : options->max_header_name_length;
        max_header_value_length = options->max_header_value_length == 0U
                                      ? SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH
                                      : options->max_header_value_length;
        max_total_header_bytes = options->max_total_header_bytes == 0U
                                     ? SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES
                                     : options->max_total_header_bytes;
        max_body_length = options->max_body_length == 0U ? SL_HTTP_DEFAULT_MAX_BODY_LENGTH
                                                         : options->max_body_length;
    }

    *ctx = (SlHttpParseContext){0};
    ctx->arena = arena;
    ctx->max_headers = max_headers;
    ctx->max_request_line_length = max_request_line_length;
    ctx->max_target_length = max_target_length;
    ctx->max_header_name_length = max_header_name_length;
    ctx->max_header_value_length = max_header_value_length;
    ctx->max_total_header_bytes = max_total_header_bytes;
    ctx->max_body_length = max_body_length;
    ctx->callback_status = sl_status_ok();
    ctx->diag_code = SL_DIAG_NONE;
}

static SlStatus sl_http_parse_with_llhttp(SlHttpParseContext* ctx, SlBytes bytes,
                                          llhttp_t* out_parser)
{
    llhttp_settings_t settings;
    llhttp_errno_t parse_error = HPE_OK;
    SlStatus status;

    if (ctx == NULL || out_parser == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_parser = (llhttp_t){0};

    status = sl_http_check_request_line_length(ctx, bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_http_settings_prepare(&settings);
    llhttp_init(out_parser, HTTP_REQUEST, &settings);
    out_parser->data = ctx;

    parse_error = llhttp_execute(out_parser, (const char*)bytes.ptr, bytes.length);
    if (parse_error != HPE_OK) {
        if (ctx->diag_code != SL_DIAG_NONE) {
            return sl_status_from_code(sl_http_status_code_for_diag(ctx->diag_code));
        }

        if (!sl_status_is_ok(ctx->callback_status)) {
            return ctx->callback_status;
        }

        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("malformed HTTP request", sizeof("malformed HTTP request") - 1U),
            sl_http_literal("provide a complete HTTP/1 request head ending with CRLF CRLF",
                            sizeof("provide a complete HTTP/1 request head ending with CRLF CRLF") -
                                1U));
    }

    parse_error = llhttp_finish(out_parser);
    if (parse_error != HPE_OK || !ctx->headers_complete || !ctx->request_complete) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("incomplete HTTP request", sizeof("incomplete HTTP request") - 1U),
            sl_http_literal("the request head must be complete before parsing",
                            sizeof("the request head must be complete before parsing") - 1U));
    }

    return sl_status_ok();
}

static SlStatus sl_http_validate_singleton_headers(SlHttpParseContext* ctx, const llhttp_t* parser)
{
    size_t index = 0U;
    size_t host_count = 0U;
    size_t content_length_count = 0U;
    size_t transfer_encoding_count = 0U;
    SlStr host_value = sl_str_empty();
    bool has_content_length = false;
    bool has_transfer_encoding = false;

    if (ctx == NULL || parser == NULL || (ctx->header_count != 0U && ctx->headers == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < ctx->header_count; index += 1U) {
        SlHttpHeader* header = &ctx->headers[index];
        if (sl_http_header_name_equal(header->name, "Host")) {
            host_count += 1U;
            host_value = header->value;
        }
        else if (sl_http_header_name_equal(header->name, "Content-Length")) {
            content_length_count += 1U;
            has_content_length = true;
        }
        else if (sl_http_header_name_equal(header->name, "Transfer-Encoding")) {
            transfer_encoding_count += 1U;
            has_transfer_encoding = true;
        }
    }

    if (parser->http_major == 1U && parser->http_minor == 1U) {
        if (host_count != 1U || sl_str_is_empty(sl_http_trim_ows(host_value))) {
            return sl_http_set_error(
                ctx, SL_DIAG_INVALID_HTTP_REQUEST,
                sl_http_literal("HTTP/1.1 request Host header is invalid",
                                sizeof("HTTP/1.1 request Host header is invalid") - 1U),
                sl_http_literal("HTTP/1.1 requests must include exactly one non-empty Host "
                                "header",
                                sizeof("HTTP/1.1 requests must include exactly one non-empty Host "
                                       "header") -
                                    1U));
        }
    }
    else if (host_count > 1U || (host_count == 1U && sl_str_is_empty(sl_http_trim_ows(host_value))))
    {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("HTTP Host header is invalid",
                            sizeof("HTTP Host header is invalid") - 1U),
            sl_http_literal("requests may include at most one non-empty Host header",
                            sizeof("requests may include at most one non-empty Host header") - 1U));
    }

    if (content_length_count > 1U) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("HTTP Content-Length header is duplicated",
                            sizeof("HTTP Content-Length header is duplicated") - 1U),
            sl_http_literal("Content-Length is a singleton framing header",
                            sizeof("Content-Length is a singleton framing header") - 1U));
    }
    if (transfer_encoding_count > 1U) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("HTTP Transfer-Encoding header is duplicated",
                            sizeof("HTTP Transfer-Encoding header is duplicated") - 1U),
            sl_http_literal("Transfer-Encoding is a singleton framing header in Sloppy's "
                            "HTTP/1.1 alpha",
                            sizeof("Transfer-Encoding is a singleton framing header in Sloppy's "
                                   "HTTP/1.1 alpha") -
                                1U));
    }
    if (has_content_length && has_transfer_encoding) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("HTTP Content-Length conflicts with Transfer-Encoding",
                            sizeof("HTTP Content-Length conflicts with Transfer-Encoding") - 1U),
            sl_http_literal("send either Content-Length or Transfer-Encoding: chunked, not both",
                            sizeof("send either Content-Length or Transfer-Encoding: chunked, not "
                                   "both") -
                                1U));
    }

    return sl_status_ok();
}

static SlStatus sl_http_validate_parsed_request(SlHttpParseContext* ctx, const llhttp_t* parser,
                                                SlHttpMethod* out_method)
{
    SlHttpMethod method = SL_HTTP_METHOD_UNKNOWN;

    if (ctx == NULL || parser == NULL || out_method == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (parser->http_major != 1U || parser->http_minor > 1U) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("unsupported or missing HTTP version",
                            sizeof("unsupported or missing HTTP version") - 1U),
            sl_http_literal("request heads must use HTTP/1.0 or HTTP/1.1",
                            sizeof("request heads must use HTTP/1.0 or HTTP/1.1") - 1U));
    }

    if (ctx->raw_target.length == 0U) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("HTTP request target must not be empty",
                            sizeof("HTTP request target must not be empty") - 1U),
            sl_http_literal("request targets must be absolute paths such as / or /users",
                            sizeof("request targets must be absolute paths such as / or /users") -
                                1U));
    }

    if (ctx->raw_target.ptr[0] != '/') {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("HTTP request target must be an absolute path",
                            sizeof("HTTP request target must be an absolute path") - 1U),
            sl_http_literal("origin-form request targets must start with /",
                            sizeof("origin-form request targets must start with /") - 1U));
    }

    {
        SlStatus header_status = sl_http_validate_singleton_headers(ctx, parser);
        if (!sl_status_is_ok(header_status)) {
            return header_status;
        }
    }

    method = sl_http_method_from_llhttp((llhttp_method_t)parser->method);
    if (method == SL_HTTP_METHOD_UNKNOWN) {
        return sl_http_set_error(
            ctx, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_http_literal("unsupported HTTP method", sizeof("unsupported HTTP method") - 1U),
            sl_http_literal(
                "this skeleton supports GET, POST, PUT, DELETE, PATCH, OPTIONS, and HEAD",
                sizeof("this skeleton supports GET, POST, PUT, DELETE, PATCH, OPTIONS, and HEAD") -
                    1U));
    }

    *out_method = method;
    return sl_status_ok();
}

SlStatus sl_http_parse_request_head(SlArena* arena, SlBytes bytes,
                                    const SlHttpParseOptions* options,
                                    SlHttpRequestHead* out_request, SlDiag* out_diag)
{
    llhttp_t parser;
    SlHttpParseContext ctx = {0};
    SlArenaMark mark = {0};
    SlHttpMethod method = SL_HTTP_METHOD_UNKNOWN;
    SlStatus status;
    SlStatus reset_status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (out_request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_request = (SlHttpRequestHead){0};
    if (arena == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(arena);
    sl_http_parse_context_init(&ctx, arena, options);

    status = sl_http_prepare_headers(arena, ctx.max_headers, &ctx.headers);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_http_init_parse_builders(&ctx);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    status = sl_http_parse_with_llhttp(&ctx, bytes, &parser);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    status = sl_http_validate_parsed_request(&ctx, &parser, &method);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    out_request->method = method;
    out_request->version_major = parser.http_major;
    out_request->version_minor = parser.http_minor;
    out_request->raw_target = ctx.raw_target;
    out_request->path = sl_http_extract_path(ctx.raw_target);
    out_request->headers = ctx.headers;
    out_request->header_count = ctx.header_count;
    out_request->body = ctx.body;
    return sl_status_ok();

failure:
    *out_request = (SlHttpRequestHead){0};
    reset_status = sl_arena_reset_to(arena, mark);
    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }

    if (ctx.diag_code != SL_DIAG_NONE) {
        SlStatus diag_status = sl_http_build_diag(arena, &ctx, out_diag);
        if (!sl_status_is_ok(diag_status)) {
            return diag_status;
        }
    }

    return status;
}
