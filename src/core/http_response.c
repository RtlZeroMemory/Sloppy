/*
 * src/core/http_response.c
 *
 * Implements EPIC-23's small native response descriptor writer for dev HTTP responses.
 * It writes deterministic HTTP/1.1 status, Connection: close, Content-Type when present,
 * bounded custom headers, Content-Length, and body bytes. It is not a streaming writer,
 * cookie layer, compression layer, or production response pipeline.
 *
 * Safety invariants:
 * - output is bounded by the caller-provided buffer;
 * - Content-Length is derived from the exact body byte count;
 * - 204 responses never write body bytes;
 * - runtime-managed response headers cannot be overridden by custom headers;
 * - user-controlled Content-Type or custom header values containing CR/LF are rejected.
 *
 * Tests: tests/unit/core/test_http_response.c.
 */
#include "sloppy/http_response.h"

#include "sloppy/builder.h"

#include <stdbool.h>

static SlBytes sl_http_response_body_from_str(SlStr text)
{
    return sl_bytes_from_parts((const unsigned char*)text.ptr, text.length);
}

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
    case 400U:
        return "Bad Request";
    case 404U:
        return "Not Found";
    case 405U:
        return "Method Not Allowed";
    case 413U:
        return "Payload Too Large";
    case 415U:
        return "Unsupported Media Type";
    case 500U:
        return "Internal Server Error";
    case 501U:
        return "Not Implemented";
    default:
        return NULL;
    }
}

static bool sl_http_response_content_type_valid(SlStr content_type)
{
    size_t index = 0U;

    if (content_type.ptr == NULL && content_type.length != 0U) {
        return false;
    }

    for (index = 0U; index < content_type.length; index += 1U) {
        if (content_type.ptr[index] == '\r' || content_type.ptr[index] == '\n') {
            return false;
        }
    }

    return true;
}

static int sl_http_response_ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static bool sl_http_response_str_iequal(SlStr left, SlStr right)
{
    size_t index = 0U;

    if ((left.ptr == NULL && left.length != 0U) || (right.ptr == NULL && right.length != 0U) ||
        left.length != right.length)
    {
        return false;
    }

    for (index = 0U; index < left.length; index += 1U) {
        if (sl_http_response_ascii_lower((unsigned char)left.ptr[index]) !=
            sl_http_response_ascii_lower((unsigned char)right.ptr[index]))
        {
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
        if (value.ptr[index] == '\r' || value.ptr[index] == '\n') {
            return false;
        }
    }

    return true;
}

static bool sl_http_response_header_managed(SlStr name)
{
    return sl_http_response_str_iequal(name, sl_str_from_cstr("Connection")) ||
           sl_http_response_str_iequal(name, sl_str_from_cstr("Content-Type")) ||
           sl_http_response_str_iequal(name, sl_str_from_cstr("Content-Length"));
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

static SlStatus sl_http_response_append_status_line(SlByteBuilder* builder, uint16_t status_code,
                                                    const char* reason)
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

    return sl_http_response_append_cstr(builder, "\r\nConnection: close\r\n");
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

SlStatus sl_http_response_write(const SlHttpResponse* response, unsigned char* buffer,
                                size_t capacity, SlBytes* out_bytes)
{
    const char* reason = NULL;
    SlBytes body = {0};
    SlByteBuilder builder = {0};
    bool has_content_type = false;
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

    body = response->status == 204U ? sl_bytes_empty() : response->body;
    has_content_type = response->content_type.length != 0U && response->status != 204U;

    status = sl_byte_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_http_response_append_status_line(&builder, response->status, reason);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (has_content_type) {
        status = sl_http_response_append_content_type(&builder, response->content_type);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status =
        sl_http_response_append_custom_headers(&builder, response->headers, response->header_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (response->status != 204U) {
        status = sl_http_response_append_content_length(&builder, body.length);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_http_response_append_cstr(&builder, "\r\n");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_byte_builder_append_bytes(&builder, body);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_bytes = sl_byte_builder_view(&builder);
    return sl_status_ok();
}
