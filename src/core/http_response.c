/*
 * src/core/http_response.c
 *
 * Implements the bounded native response descriptor writer for development HTTP responses.
 * It writes deterministic HTTP/1.1 status, managed Connection policy, Content-Type when
 * present, bounded custom headers, Content-Length, and body bytes. Stream responses are
 * bounded descriptors at this layer; live socket push remains transport-owned.
 *
 * Safety invariants:
 * - output is bounded by the caller-provided buffer;
 * - Content-Length is derived from the exact body byte count;
 * - 204 responses never write body bytes;
 * - runtime-managed response headers cannot be overridden by custom headers;
 * - user-controlled Content-Type or custom header values containing control bytes are
 *   rejected, except HTAB inside header values.
 *
 * Tests: tests/unit/core/test_http_response.c.
 */
#include "sloppy/http_response.h"

#include "sloppy/breadcrumbs.h"
#include "sloppy/builder.h"
#include "sloppy/checked_math.h"

#include <stdbool.h>

static SlBytes sl_http_response_body_from_str(SlStr text)
{
    return sl_bytes_from_parts((const unsigned char*)text.ptr, text.length);
}

static SlStatus sl_http_response_stream_validate_and_measure(const SlHttpResponse* response,
                                                             size_t* out_length);

SlHttpResponse sl_http_response_text(uint16_t status, SlStr body)
{
    SlHttpResponse response = {0};

    response.status = status;
    response.kind = SL_HTTP_RESPONSE_TEXT;
    response.content_type = sl_str_from_cstr("text/plain; charset=utf-8");
    response.body = sl_http_response_body_from_str(body);
    return response;
}

SlHttpResponse sl_http_response_json(uint16_t status, SlBytes body)
{
    SlHttpResponse response = {0};

    response.status = status;
    response.kind = SL_HTTP_RESPONSE_JSON;
    response.content_type = sl_str_from_cstr("application/json; charset=utf-8");
    response.body = body;
    return response;
}

SlHttpResponse sl_http_response_bytes(uint16_t status, SlStr content_type, SlBytes body)
{
    SlHttpResponse response = {0};

    response.status = status;
    response.kind = SL_HTTP_RESPONSE_BYTES;
    response.content_type = content_type;
    response.body = body;
    return response;
}

SlHttpResponse sl_http_response_empty(uint16_t status)
{
    SlHttpResponse response = {0};

    response.status = status;
    response.kind = SL_HTTP_RESPONSE_EMPTY;
    response.body = sl_bytes_empty();
    return response;
}

SlHttpResponse sl_http_response_problem(uint16_t status, SlBytes body)
{
    SlHttpResponse response = {0};

    response.status = status;
    response.kind = SL_HTTP_RESPONSE_PROBLEM;
    response.content_type = sl_str_from_cstr("application/problem+json; charset=utf-8");
    response.body = body;
    return response;
}

SlHttpResponse sl_http_response_stream(uint16_t status, SlStr content_type,
                                       const SlStreamChunk* chunks, size_t chunk_count)
{
    SlHttpResponse response = {0};

    response.status = status;
    response.kind = SL_HTTP_RESPONSE_STREAM;
    response.content_type = content_type;
    response.stream_chunks = chunk_count == 0U ? NULL : chunks;
    response.stream_chunk_count = chunks == NULL ? 0U : chunk_count;
    return response;
}

static SlStreamStatus sl_http_response_stream_read(SlReadableStream* stream,
                                                   SlStreamReadResult* out)
{
    SlHttpResponseStreamReadable* adapter = (SlHttpResponseStreamReadable*)stream->user;

    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }

    while (adapter->chunk_index < adapter->chunk_count) {
        const SlStreamChunk* source = &adapter->chunks[adapter->chunk_index];
        size_t remaining = 0U;
        size_t length = 0U;

        if (source->bytes.length != 0U && source->bytes.ptr == NULL) {
            return SL_STREAM_STATUS_INVALID_STATE;
        }
        if (adapter->chunk_offset >= source->bytes.length) {
            adapter->chunk_index += 1U;
            adapter->chunk_offset = 0U;
            continue;
        }
        remaining = source->bytes.length - adapter->chunk_offset;
        length = remaining > stream->max_chunk_bytes ? stream->max_chunk_bytes : remaining;
        out->chunk.bytes = sl_bytes_from_parts(source->bytes.ptr + adapter->chunk_offset, length);
        adapter->chunk_offset += length;
        if (adapter->chunk_offset >= source->bytes.length) {
            adapter->chunk_index += 1U;
            adapter->chunk_offset = 0U;
        }
        return SL_STREAM_STATUS_OK;
    }

    return SL_STREAM_STATUS_EOF;
}

static const SlReadableStreamVTable sl_http_response_stream_readable_vtable = {
    sl_http_response_stream_read, NULL, NULL, NULL, "http-response-stream"};

SlStatus sl_http_response_stream_readable_init(SlHttpResponseStreamReadable* adapter,
                                               const SlHttpResponse* response,
                                               const SlStreamOptions* options,
                                               SlReadableStream* out_stream)
{
    size_t stream_body_length = 0U;
    SlStatus status;

    if (adapter == NULL || response == NULL || out_stream == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (response->kind != SL_HTTP_RESPONSE_STREAM) {
        return sl_status_from_code(SL_STATUS_WRONG_RESOURCE_KIND);
    }
    status = sl_http_response_stream_validate_and_measure(response, &stream_body_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *adapter = (SlHttpResponseStreamReadable){0};
    adapter->chunks = response->stream_chunks;
    adapter->chunk_count = response->stream_chunk_count;
    return sl_readable_stream_init(out_stream, &sl_http_response_stream_readable_vtable, adapter,
                                   options);
}

static const char* sl_http_response_reason(uint16_t status)
{
    switch (status) {
    case 200U:
        return "OK";
    case 201U:
        return "Created";
    case 202U:
        return "Accepted";
    case 204U:
        return "No Content";
    case 304U:
        return "Not Modified";
    case 400U:
        return "Bad Request";
    case 401U:
        return "Unauthorized";
    case 404U:
        return "Not Found";
    case 405U:
        return "Method Not Allowed";
    case 408U:
        return "Request Timeout";
    case 413U:
        return "Payload Too Large";
    case 415U:
        return "Unsupported Media Type";
    case 417U:
        return "Expectation Failed";
    case 500U:
        return "Internal Server Error";
    case 503U:
        return "Service Unavailable";
    case 501U:
        return "Not Implemented";
    default:
        return NULL;
    }
}

static bool sl_http_response_status_has_no_body(uint16_t status)
{
    return status == 204U || status == 304U;
}

static SlStatus sl_http_response_stream_validate_and_measure(const SlHttpResponse* response,
                                                             size_t* out_length)
{
    size_t total = 0U;

    if (response == NULL || out_length == NULL || response->kind != SL_HTTP_RESPONSE_STREAM) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (response->stream_chunks == NULL && response->stream_chunk_count != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (size_t index = 0U; index < response->stream_chunk_count; index += 1U) {
        SlBytes chunk = response->stream_chunks[index].bytes;
        SlStatus status;

        if (chunk.length != 0U && chunk.ptr == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (chunk.length != 0U && sl_http_response_status_has_no_body(response->status)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_checked_add_size(total, chunk.length, &total);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    *out_length = total;
    return sl_status_ok();
}

static bool sl_http_response_content_type_valid(SlStr content_type)
{
    size_t index = 0U;

    if (content_type.ptr == NULL && content_type.length != 0U) {
        return false;
    }

    for (index = 0U; index < content_type.length; index += 1U) {
        unsigned char ch = (unsigned char)content_type.ptr[index];
        if (ch < 0x20U || ch == 0x7FU) {
            return false;
        }
    }

    return true;
}

static bool sl_http_response_header_name_char_valid(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' || ch == '\'' ||
           ch == '*' || ch == '+' || ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
           ch == '`' || ch == '|' || ch == '~';
}

static bool sl_http_response_header_name_valid(SlStr name)
{
    size_t index = 0U;

    if (name.ptr == NULL || name.length == 0U) {
        return false;
    }

    for (index = 0U; index < name.length; index += 1U) {
        if (!sl_http_response_header_name_char_valid(name.ptr[index])) {
            return false;
        }
    }

    return true;
}

static bool sl_http_response_header_value_valid(SlStr value)
{
    size_t index = 0U;

    if (value.ptr == NULL && value.length != 0U) {
        return false;
    }

    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if ((ch < 0x20U && ch != '\t') || ch == 0x7FU) {
            return false;
        }
    }

    return true;
}

static bool sl_http_response_header_managed(SlStr name)
{
    return sl_str_equal_ci_ascii(name, sl_str_from_cstr("Connection")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Keep-Alive")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Content-Type")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Transfer-Encoding")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Content-Length"));
}

static bool sl_http_response_headers_valid(const SlHttpResponse* response)
{
    size_t index = 0U;

    if (response == NULL || (response->header_count != 0U && response->headers == NULL)) {
        return false;
    }

    for (index = 0U; index < response->header_count; index += 1U) {
        const SlHttpHeader* header = &response->headers[index];
        if (!sl_http_response_header_name_valid(header->name) ||
            sl_http_response_header_managed(header->name) ||
            !sl_http_response_header_value_valid(header->value))
        {
            return false;
        }
    }

    return true;
}

static SlStatus sl_http_response_append_cstr(SlByteBuilder* builder, const char* text)
{
    if (text == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_byte_builder_append_bytes(
        builder, sl_bytes_from_parts((const unsigned char*)text, sl_str_from_cstr(text).length));
}

static SlStatus sl_http_response_append_str(SlByteBuilder* builder, SlStr text)
{
    return sl_byte_builder_append_bytes(
        builder, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length));
}

static SlStatus sl_http_response_append_size(SlByteBuilder* builder, size_t value)
{
    char digits[32];
    size_t count = 0U;
    SlStatus status;

    do {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    } while (value != 0U && count < sizeof(digits));

    while (count > 0U) {
        count -= 1U;
        status = sl_byte_builder_append_byte(builder, (unsigned char)digits[count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus
sl_http_response_append_status_line(SlByteBuilder* builder, uint16_t status_code,
                                    const char* reason,
                                    SlHttpResponseConnectionPolicy connection_policy)
{
    SlStatus status;

    status = sl_http_response_append_cstr(builder, "HTTP/1.1 ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_response_append_size(builder, status_code);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_response_append_cstr(builder, " ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_response_append_cstr(builder, reason);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_http_response_append_cstr(builder, "\r\nConnection: ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_response_append_cstr(
        builder,
        connection_policy == SL_HTTP_RESPONSE_CONNECTION_KEEP_ALIVE ? "keep-alive" : "close");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http_response_append_cstr(builder, "\r\n");
}

static SlStatus sl_http_response_append_content_type(SlByteBuilder* builder, SlStr content_type)
{
    SlStatus status = sl_http_response_append_cstr(builder, "Content-Type: ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_response_append_str(builder, content_type);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http_response_append_cstr(builder, "\r\n");
}

static SlStatus sl_http_response_append_custom_headers(SlByteBuilder* builder,
                                                       const SlHttpHeader* headers, size_t count)
{
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        SlStatus status = sl_http_response_append_str(builder, headers[index].name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_response_append_cstr(builder, ": ");
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_response_append_str(builder, headers[index].value);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_response_append_cstr(builder, "\r\n");
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_http_response_append_content_length(SlByteBuilder* builder, size_t body_length)
{
    SlStatus status = sl_http_response_append_cstr(builder, "Content-Length: ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_response_append_size(builder, body_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http_response_append_cstr(builder, "\r\n");
}

static SlStatus sl_http_response_append_stream_body(SlByteBuilder* builder,
                                                    const SlHttpResponse* response)
{
    for (size_t index = 0U; index < response->stream_chunk_count; index += 1U) {
        SlBytes chunk = response->stream_chunks[index].bytes;
        SlStatus status;

        if (chunk.length == 0U) {
            continue;
        }
        status = sl_byte_builder_append_bytes(builder, chunk);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

static bool sl_http_response_connection_policy_valid(SlHttpResponseConnectionPolicy policy)
{
    return policy == SL_HTTP_RESPONSE_CONNECTION_CLOSE ||
           policy == SL_HTTP_RESPONSE_CONNECTION_KEEP_ALIVE;
}

static SlStatus sl_http_response_append_head(SlByteBuilder* builder, const SlHttpResponse* response,
                                             const char* reason,
                                             SlHttpResponseConnectionPolicy connection_policy,
                                             bool has_content_type, size_t body_length)
{
    SlStatus status =
        sl_http_response_append_status_line(builder, response->status, reason, connection_policy);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (has_content_type) {
        status = sl_http_response_append_content_type(builder, response->content_type);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status =
        sl_http_response_append_custom_headers(builder, response->headers, response->header_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_http_response_status_has_no_body(response->status)) {
        status = sl_http_response_append_content_length(builder, body_length);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_http_response_append_cstr(builder, "\r\n");
}

SlStatus sl_http_response_write_with_options(const SlHttpResponse* response,
                                             const SlHttpResponseWriteOptions* options,
                                             unsigned char* buffer, size_t capacity,
                                             SlBytes* out_bytes)
{
    const char* reason = NULL;
    SlBytes body = {0};
    SlBytes wire_body = {0};
    SlByteBuilder builder = {0};
    bool has_content_type = false;
    bool is_stream_response = false;
    bool no_body_status = false;
    bool suppress_body = false;
    size_t stream_body_length = 0U;
    SlHttpResponseConnectionPolicy connection_policy = SL_HTTP_RESPONSE_CONNECTION_CLOSE;
    SlStatus status;

    if (response == NULL || buffer == NULL || out_bytes == NULL || capacity == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_bytes = sl_bytes_empty();
    reason = sl_http_response_reason(response->status);
    if (reason == NULL) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    if (!sl_http_response_content_type_valid(response->content_type)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_http_response_headers_valid(response)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (options != NULL && !sl_http_response_connection_policy_valid(options->connection)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (options != NULL) {
        connection_policy = options->connection;
        suppress_body = options->suppress_body;
    }

    is_stream_response = response->kind == SL_HTTP_RESPONSE_STREAM;
    no_body_status = sl_http_response_status_has_no_body(response->status);
    if (is_stream_response) {
        status = sl_http_response_stream_validate_and_measure(response, &stream_body_length);
        if (!sl_status_is_ok(status)) {
            sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_STREAM,
                                        SL_BREADCRUMB_EVENT_STREAM_FAILURE, sl_status_code(status),
                                        0U, 0U, 0U, 0U,
                                        sl_str_from_cstr("stream response validation failed"));
            return status;
        }
    }

    body = no_body_status ? sl_bytes_empty() : response->body;
    wire_body = suppress_body || is_stream_response ? sl_bytes_empty() : body;
    has_content_type = response->content_type.length != 0U && !no_body_status;

    status = sl_byte_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_http_response_append_head(&builder, response, reason, connection_policy,
                                          has_content_type,
                                          is_stream_response ? stream_body_length : body.length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (is_stream_response && !suppress_body && !no_body_status) {
        status = sl_http_response_append_stream_body(&builder, response);
    }
    else {
        status = sl_byte_builder_append_bytes(&builder, wire_body);
    }
    if (!sl_status_is_ok(status)) {
        if (is_stream_response && sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED) {
            sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_STREAM,
                                        SL_BREADCRUMB_EVENT_STREAM_BACKPRESSURE,
                                        SL_STATUS_CAPACITY_EXCEEDED, 0U, 0U, 0U, 0U,
                                        sl_str_from_cstr("response buffer capacity exceeded"));
        }
        return status;
    }

    *out_bytes = sl_byte_builder_view(&builder);
    return sl_status_ok();
}

SlStatus sl_http_response_write(const SlHttpResponse* response, unsigned char* buffer,
                                size_t capacity, SlBytes* out_bytes)
{
    return sl_http_response_write_with_options(response, NULL, buffer, capacity, out_bytes);
}
