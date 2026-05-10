#include "sloppy/http2_mapping.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"

#include <stdint.h>

static bool sl_http2_mapping_valid_str(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static bool sl_http2_mapping_valid_bytes(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

static bool sl_http2_mapping_status_supported(uint16_t status)
{
    switch (status) {
    case 200U:
    case 201U:
    case 202U:
    case 204U:
    case 304U:
    case 400U:
    case 404U:
    case 405U:
    case 408U:
    case 413U:
    case 415U:
    case 417U:
    case 500U:
    case 501U:
        return true;
    default:
        return false;
    }
}

static bool sl_http2_mapping_status_has_no_body(uint16_t status)
{
    return status == 204U || status == 304U;
}

static bool sl_http2_header_value_valid(SlStr value)
{
    if (!sl_http2_mapping_valid_str(value)) {
        return false;
    }
    for (size_t index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if ((ch < 0x20U && ch != '\t') || ch == 0x7fU) {
            return false;
        }
    }
    return true;
}

static bool sl_http2_header_name_char_valid(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '!' || ch == '#' ||
           ch == '$' || ch == '%' || ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
           ch == '-' || ch == '.' || ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~';
}

static bool sl_http2_header_name_valid(SlStr name)
{
    if (name.ptr == NULL || name.length == 0U) {
        return false;
    }
    for (size_t index = 0U; index < name.length; index += 1U) {
        if (!sl_http2_header_name_char_valid(name.ptr[index])) {
            return false;
        }
    }
    return true;
}

static bool sl_http2_response_header_managed(SlStr name)
{
    return sl_str_equal_ci_ascii(name, sl_str_from_cstr("connection")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("keep-alive")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("content-type")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("transfer-encoding")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("content-length")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("upgrade")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("proxy-connection"));
}

static bool sl_http2_request_header_forbidden(SlStr name, SlStr value)
{
    if (sl_str_equal_ci_ascii(name, sl_str_from_cstr("connection")) ||
        sl_str_equal_ci_ascii(name, sl_str_from_cstr("keep-alive")) ||
        sl_str_equal_ci_ascii(name, sl_str_from_cstr("proxy-connection")) ||
        sl_str_equal_ci_ascii(name, sl_str_from_cstr("transfer-encoding")) ||
        sl_str_equal_ci_ascii(name, sl_str_from_cstr("upgrade")))
    {
        return true;
    }
    if (sl_str_equal_ci_ascii(name, sl_str_from_cstr("te")) &&
        !sl_str_equal_ci_ascii(value, sl_str_from_cstr("trailers")))
    {
        return true;
    }
    return false;
}

static SlStatus sl_http2_copy_header(SlArena* arena, SlStr name, SlStr value, SlHttpHeader* out)
{
    SlStatus status = sl_status_ok();
    SlStr copied_name = {0};
    SlStr copied_value = {0};

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_str_copy_view_to_arena(arena, name, &copied_name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_view_to_arena(arena, value, &copied_value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = (SlHttpHeader){.name = copied_name, .value = copied_value};
    return sl_status_ok();
}

static SlStatus sl_http2_copy_h2_header(SlArena* arena, SlStr name, SlStr value,
                                        SlHttp2HeaderField* out)
{
    SlStatus status = sl_status_ok();
    SlStr copied_name = {0};
    SlStr copied_value = {0};

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_str_copy_view_to_arena(arena, name, &copied_name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_view_to_arena(arena, value, &copied_value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = (SlHttp2HeaderField){.name = copied_name, .value = copied_value, .sensitive = false};
    return sl_status_ok();
}

static SlStr sl_http2_path_from_target(SlStr target)
{
    for (size_t index = 0U; index < target.length; index += 1U) {
        if (target.ptr[index] == '?') {
            return sl_str_from_parts(target.ptr, index);
        }
    }
    return target;
}

static bool sl_http2_parse_decimal_size(SlStr value, size_t* out)
{
    size_t result = 0U;

    if (out == NULL || value.ptr == NULL || value.length == 0U) {
        return false;
    }
    for (size_t index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        size_t digit = 0U;
        if (ch < (unsigned char)'0' || ch > (unsigned char)'9') {
            return false;
        }
        digit = (size_t)(ch - (unsigned char)'0');
        if (result > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        result = result * 10U + digit;
    }
    *out = result;
    return true;
}

static SlHttpParseOptions sl_http2_effective_parse_options(const SlHttpConnection* connection)
{
    SlHttpParseOptions options = {0};

    options.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    options.max_request_line_length = SL_HTTP_DEFAULT_MAX_REQUEST_LINE_LENGTH;
    options.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    options.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    options.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    options.max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
    options.max_body_length = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;

    if (connection != NULL && connection->backend != NULL) {
        SlHttpParseOptions configured = connection->backend->options.parse;
        options.max_headers = configured.max_headers;
        options.max_request_line_length = configured.max_request_line_length == 0U
                                              ? options.max_request_line_length
                                              : configured.max_request_line_length;
        options.max_target_length = configured.max_target_length == 0U
                                        ? options.max_target_length
                                        : configured.max_target_length;
        options.max_header_name_length = configured.max_header_name_length == 0U
                                             ? options.max_header_name_length
                                             : configured.max_header_name_length;
        options.max_header_value_length = configured.max_header_value_length == 0U
                                              ? options.max_header_value_length
                                              : configured.max_header_value_length;
        options.max_total_header_bytes = configured.max_total_header_bytes == 0U
                                             ? options.max_total_header_bytes
                                             : configured.max_total_header_bytes;
        options.max_body_length =
            configured.max_body_length == 0U ? options.max_body_length : configured.max_body_length;
    }

    return options;
}

SlStatus sl_http2_request_from_headers(SlArena* arena, SlHttpConnection* connection,
                                       const SlHttp2HeaderList* headers, SlBytes body,
                                       SlHttpRequestLifecycle* out_request, SlDiag* out_diag)
{
    SlStatus status = sl_status_ok();
    SlHttpParseOptions limits = {0};
    SlStr method = {0};
    SlStr scheme = {0};
    SlStr target = {0};
    SlStr authority = {0};
    SlStr host = {0};
    SlStr effective_authority = {0};
    SlStr content_type = {0};
    SlStr content_length = {0};
    SlHttpHeader* copied_headers = NULL;
    void* header_storage = NULL;
    size_t regular_header_count = 0U;
    size_t total_header_bytes = 0U;
    bool has_content_type = false;
    bool has_content_length = false;
    bool regular_seen = false;
    size_t declared_content_length = 0U;
    SlHttpBodyReader reader = {0};

    if (arena == NULL || connection == NULL || headers == NULL || out_request == NULL ||
        (headers->count != 0U && headers->fields == NULL) || !sl_http2_mapping_valid_bytes(body))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    limits = sl_http2_effective_parse_options(connection);
    if (headers->count > limits.max_headers + 4U || body.length > limits.max_body_length) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    for (size_t index = 0U; index < headers->count; index += 1U) {
        const SlHttp2HeaderField* header = &headers->fields[index];
        if (!sl_http2_mapping_valid_str(header->name) ||
            !sl_http2_mapping_valid_str(header->value) ||
            header->name.length > limits.max_header_name_length ||
            header->value.length > limits.max_header_value_length ||
            header->name.length > SIZE_MAX - header->value.length ||
            total_header_bytes > SIZE_MAX - (header->name.length + header->value.length))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        total_header_bytes += header->name.length + header->value.length;
        if (total_header_bytes > limits.max_total_header_bytes) {
            return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
        }

        if (sl_str_equal(header->name, sl_str_from_cstr(":method"))) {
            if (regular_seen) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (!sl_str_is_empty(method)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            method = header->value;
        }
        else if (sl_str_equal(header->name, sl_str_from_cstr(":scheme"))) {
            if (regular_seen) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (!sl_str_is_empty(scheme)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            scheme = header->value;
        }
        else if (sl_str_equal(header->name, sl_str_from_cstr(":path"))) {
            if (regular_seen) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (!sl_str_is_empty(target)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            target = header->value;
        }
        else if (sl_str_equal(header->name, sl_str_from_cstr(":authority"))) {
            if (regular_seen) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (!sl_str_is_empty(authority)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            authority = header->value;
        }
        else {
            regular_seen = true;
            if (header->name.length != 0U && header->name.ptr[0] == ':') {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (!sl_http2_header_name_valid(header->name) ||
                !sl_http2_header_value_valid(header->value) ||
                sl_http2_request_header_forbidden(header->name, header->value))
            {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (sl_str_equal_ci_ascii(header->name, sl_str_from_cstr("content-type"))) {
                content_type = header->value;
                has_content_type = true;
            }
            if (sl_str_equal_ci_ascii(header->name, sl_str_from_cstr("content-length"))) {
                if (has_content_length ||
                    !sl_http2_parse_decimal_size(header->value, &declared_content_length))
                {
                    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
                }
                content_length = header->value;
                has_content_length = true;
            }
            if (sl_str_equal_ci_ascii(header->name, sl_str_from_cstr("host"))) {
                if (!sl_str_is_empty(host)) {
                    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
                }
                host = header->value;
            }
            regular_header_count += 1U;
        }
    }

    effective_authority = sl_str_is_empty(authority) ? host : authority;
    if (method.ptr == NULL || method.length == 0U || scheme.ptr == NULL || scheme.length == 0U ||
        target.ptr == NULL || target.length == 0U || target.ptr[0] != '/' ||
        target.length > limits.max_target_length ||
        (!sl_str_equal(scheme, sl_str_from_cstr("http")) &&
         !sl_str_equal(scheme, sl_str_from_cstr("https"))) ||
        (connection->scheme.ptr != NULL && connection->scheme.length != 0U &&
         !sl_str_equal(scheme, connection->scheme)) ||
        sl_str_equal_ci_ascii(method, sl_str_from_cstr("CONNECT")) ||
        (sl_str_is_empty(authority) && sl_str_is_empty(host)) ||
        (!sl_str_is_empty(authority) && !sl_str_is_empty(host) && !sl_str_equal(host, authority)) ||
        (has_content_length && declared_content_length != body.length))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_request_begin(connection, arena, out_request, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_http_method_from_str(method, &out_request->head.method);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(out_request, out_diag);
        return status;
    }

    if (!sl_str_is_empty(effective_authority)) {
        regular_header_count += 1U;
    }
    if (regular_header_count > limits.max_headers) {
        (void)sl_http_request_fail(out_request, out_diag);
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    if (regular_header_count != 0U) {
        size_t header_bytes = 0U;
        status = sl_checked_array_size(regular_header_count, sizeof(SlHttpHeader), &header_bytes);
        if (!sl_status_is_ok(status)) {
            (void)sl_http_request_fail(out_request, out_diag);
            return status;
        }
        status = sl_arena_alloc(arena, header_bytes, _Alignof(SlHttpHeader), &header_storage);
        if (!sl_status_is_ok(status)) {
            (void)sl_http_request_fail(out_request, out_diag);
            return status;
        }
    }

    copied_headers = (SlHttpHeader*)header_storage;
    regular_header_count = 0U;
    if (!sl_str_is_empty(effective_authority)) {
        status = sl_http2_copy_header(arena, sl_str_from_cstr("host"), effective_authority,
                                      &copied_headers[regular_header_count]);
        if (!sl_status_is_ok(status)) {
            (void)sl_http_request_fail(out_request, out_diag);
            return status;
        }
        regular_header_count += 1U;
    }
    for (size_t index = 0U; index < headers->count; index += 1U) {
        const SlHttp2HeaderField* header = &headers->fields[index];
        if (header->name.length != 0U && header->name.ptr[0] == ':') {
            continue;
        }
        if (!sl_str_is_empty(effective_authority) &&
            sl_str_equal_ci_ascii(header->name, sl_str_from_cstr("host")))
        {
            continue;
        }
        status = sl_http2_copy_header(arena, header->name, header->value,
                                      &copied_headers[regular_header_count]);
        if (!sl_status_is_ok(status)) {
            (void)sl_http_request_fail(out_request, out_diag);
            return status;
        }
        regular_header_count += 1U;
    }

    status = sl_str_copy_view_to_arena(arena, target, &out_request->head.raw_target);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(out_request, out_diag);
        return status;
    }
    status = sl_str_copy_view_to_arena(arena, sl_http2_path_from_target(target),
                                       &out_request->head.path);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(out_request, out_diag);
        return status;
    }

    out_request->head.version_major = 2U;
    out_request->head.version_minor = 0U;
    out_request->head.headers = copied_headers;
    out_request->head.header_count = regular_header_count;
    out_request->state = SL_HTTP_REQUEST_STATE_READING;
    connection->state = SL_HTTP_CONNECTION_STATE_READING_REQUEST;

    status = sl_http_request_body_reader_begin(
        out_request, has_content_type ? content_type : sl_str_from_cstr("application/octet-stream"),
        body.length, &reader, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(out_request, out_diag);
        return status;
    }
    if (!sl_str_is_empty(content_length)) {
        (void)content_length;
    }
    status = sl_http_request_body_reader_append(&reader, body, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(out_request, out_diag);
        return status;
    }
    status = sl_http_request_body_reader_finish(&reader, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(out_request, out_diag);
        return status;
    }

    return sl_status_ok();
}

static SlStatus sl_http2_response_body(SlArena* arena, const SlHttpResponse* response,
                                       bool suppress_body, size_t max_body_bytes, SlBytes* out_body)
{
    SlStatus status = sl_status_ok();
    SlByteBuilder builder = {0};

    if (arena == NULL || response == NULL || out_body == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (suppress_body || sl_http2_mapping_status_has_no_body(response->status)) {
        *out_body = sl_bytes_empty();
        return sl_status_ok();
    }
    if (response->kind != SL_HTTP_RESPONSE_STREAM) {
        if (!sl_http2_mapping_valid_bytes(response->body) || response->body.length > max_body_bytes)
        {
            return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
        }
        *out_body = response->body;
        return sl_status_ok();
    }

    status = sl_byte_builder_init_arena(&builder, arena, 256U, max_body_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < response->stream_chunk_count; index += 1U) {
        status = sl_byte_builder_append_bytes(&builder, response->stream_chunks[index].bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_body = sl_byte_builder_view(&builder);
    return sl_status_ok();
}

SlStatus sl_http2_response_to_headers(SlArena* arena, const SlHttpResponse* response,
                                      bool suppress_body, size_t max_body_bytes,
                                      SlHttp2HeaderList* out_headers, SlBytes* out_body)
{
    SlStatus status = sl_status_ok();
    SlHttp2HeaderField* fields = NULL;
    void* field_storage = NULL;
    size_t field_count = 0U;
    size_t field_capacity = 0U;
    SlBytes body = {0};
    char status_digits[8];
    char length_digits[32];
    SlStr status_value = {0};
    SlStr length_value = {0};
    bool no_body_status = false;

    if (arena == NULL || response == NULL || out_headers == NULL || out_body == NULL ||
        (response->header_count != 0U && response->headers == NULL) ||
        !sl_http2_mapping_status_supported(response->status) ||
        !sl_http2_header_value_valid(response->content_type))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    no_body_status = sl_http2_mapping_status_has_no_body(response->status);
    status = sl_http2_response_body(arena, response, suppress_body, max_body_bytes, &body);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_add3_size(response->header_count, 1U,
                                  response->content_type.length == 0U ? 0U : 1U, &field_capacity);
    if (sl_status_is_ok(status)) {
        status = sl_checked_add_size(field_capacity, no_body_status ? 0U : 1U, &field_capacity);
    }
    if (sl_status_is_ok(status)) {
        size_t field_bytes = 0U;
        status = sl_checked_array_size(field_capacity, sizeof(SlHttp2HeaderField), &field_bytes);
        if (sl_status_is_ok(status)) {
            status =
                sl_arena_alloc(arena, field_bytes, _Alignof(SlHttp2HeaderField), &field_storage);
        }
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    fields = (SlHttp2HeaderField*)field_storage;

    status =
        sl_string_format_u64(status_digits, sizeof(status_digits), response->status, &status_value);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http2_copy_h2_header(arena, sl_str_from_cstr(":status"), status_value,
                                     &fields[field_count]);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    field_count += 1U;

    if (!no_body_status && response->content_type.length != 0U) {
        status = sl_http2_copy_h2_header(arena, sl_str_from_cstr("content-type"),
                                         response->content_type, &fields[field_count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        field_count += 1U;
    }

    for (size_t index = 0U; index < response->header_count; index += 1U) {
        const SlHttpHeader* header = &response->headers[index];
        if (!sl_http2_header_name_valid(header->name) ||
            !sl_http2_header_value_valid(header->value) ||
            sl_http2_response_header_managed(header->name))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_http2_copy_h2_header(arena, header->name, header->value, &fields[field_count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        field_count += 1U;
    }

    if (!no_body_status) {
        status = sl_string_format_size(length_digits, sizeof(length_digits),
                                       suppress_body ? response->body.length : body.length,
                                       &length_value);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http2_copy_h2_header(arena, sl_str_from_cstr("content-length"), length_value,
                                         &fields[field_count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        field_count += 1U;
    }

    *out_headers = (SlHttp2HeaderList){.fields = fields, .count = field_count};
    *out_body = body;
    return sl_status_ok();
}
