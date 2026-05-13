#include "sloppy/websocket.h"

#include "sloppy/builder.h"

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <string.h>

static SlStr sl_ws_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlStatus sl_ws_diag(SlDiag* out_diag, SlStr message)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = SL_DIAG_INVALID_HTTP_REQUEST;
        out_diag->message = message;
    }
    return sl_status_ok();
}

static bool sl_ws_header_value(const SlHttpRequestHead* request, SlStr name, SlStr* out_value)
{
    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (request == NULL || out_value == NULL ||
        (request->header_count != 0U && request->headers == NULL))
    {
        return false;
    }
    for (size_t index = 0U; index < request->header_count; index += 1U) {
        if (sl_str_equal_ci_ascii(request->headers[index].name, name)) {
            *out_value = request->headers[index].value;
            return true;
        }
    }
    return false;
}

static SlStr sl_ws_trim(SlStr value)
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

static bool sl_ws_header_has_token(SlStr value, SlStr token)
{
    size_t cursor = 0U;

    if ((value.ptr == NULL && value.length != 0U) || token.ptr == NULL || token.length == 0U) {
        return false;
    }
    while (cursor <= value.length) {
        size_t start = cursor;
        size_t end = cursor;
        while (end < value.length && value.ptr[end] != ',') {
            end += 1U;
        }
        if (sl_str_equal_ci_ascii(sl_ws_trim(sl_str_from_parts(value.ptr + start, end - start)),
                                  token))
        {
            return true;
        }
        if (end == value.length) {
            break;
        }
        cursor = end + 1U;
    }
    return false;
}

static bool sl_ws_decode_client_key(SlStr key)
{
    unsigned char decoded[32] = {0};
    int decoded_len = 0;
    size_t padding = 0U;

    if (key.ptr == NULL || key.length == 0U || key.length > 64U) {
        return false;
    }
    if (key.ptr[key.length - 1U] == '=') {
        padding += 1U;
    }
    if (key.length > 1U && key.ptr[key.length - 2U] == '=') {
        padding += 1U;
    }
    decoded_len = EVP_DecodeBlock(decoded, (const unsigned char*)key.ptr, (int)key.length);
    if (decoded_len < 0 || (size_t)decoded_len < padding) {
        return false;
    }
    return (size_t)decoded_len - padding == 16U;
}

static SlStatus sl_ws_accept_key(SlArena* arena, SlStr key, SlStr* out_accept)
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[SHA_DIGEST_LENGTH] = {0};
    unsigned char accept[32] = {0};
    unsigned char* input = NULL;
    char* copied = NULL;
    size_t input_length = key.length + sizeof(guid) - 1U;
    int encoded = 0;
    SlStatus status;

    if (arena == NULL || out_accept == NULL || key.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, input_length, 1U, (void**)&input);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < key.length; index += 1U) {
        input[index] = (unsigned char)key.ptr[index];
    }
    for (size_t index = 0U; index < sizeof(guid) - 1U; index += 1U) {
        input[key.length + index] = (unsigned char)guid[index];
    }
    SHA1(input, input_length, digest);
    encoded = EVP_EncodeBlock(accept, digest, SHA_DIGEST_LENGTH);
    if (encoded <= 0) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    status = sl_arena_alloc(arena, (size_t)encoded, 1U, (void**)&copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < (size_t)encoded; index += 1U) {
        copied[index] = (char)accept[index];
    }
    *out_accept = sl_str_from_parts(copied, (size_t)encoded);
    return sl_status_ok();
}

static bool sl_ws_origin_allowed(const SlPlanWebSocketOptions* options, SlStr origin)
{
    if (options == NULL || sl_str_is_empty(origin) ||
        options->origin_policy == SL_PLAN_WEBSOCKET_ORIGINS_NONE ||
        options->origin_policy == SL_PLAN_WEBSOCKET_ORIGINS_ANY)
    {
        return true;
    }
    if (options->origin_policy != SL_PLAN_WEBSOCKET_ORIGINS_LIST ||
        (options->origin_count != 0U && options->origins == NULL))
    {
        return false;
    }
    for (size_t index = 0U; index < options->origin_count; index += 1U) {
        if (sl_str_equal(options->origins[index], origin)) {
            return true;
        }
    }
    return false;
}

static SlStr sl_ws_select_protocol(const SlPlanWebSocketOptions* options, SlStr requested)
{
    if (options == NULL || options->protocol_count == 0U) {
        return sl_str_empty();
    }
    if (options->protocols == NULL) {
        return sl_str_empty();
    }
    size_t cursor = 0U;
    while (cursor <= requested.length) {
        size_t start = cursor;
        size_t end = cursor;
        SlStr token = {0};
        while (end < requested.length && requested.ptr[end] != ',') {
            end += 1U;
        }
        token = sl_ws_trim(sl_str_from_parts(requested.ptr + start, end - start));
        for (size_t index = 0U; index < options->protocol_count; index += 1U) {
            if (sl_str_equal(options->protocols[index], token)) {
                return options->protocols[index];
            }
        }
        if (end == requested.length) {
            break;
        }
        cursor = end + 1U;
    }
    return sl_str_empty();
}

static SlStatus sl_ws_append(SlByteBuilder* builder, SlStr value)
{
    return sl_byte_builder_append_bytes(
        builder, sl_bytes_from_parts((const unsigned char*)value.ptr, value.length));
}

static SlStatus sl_ws_build_response(SlArena* arena, SlStr accept, SlStr protocol, SlBytes* out)
{
    SlByteBuilder builder = {0};
    SlStatus status;

    status = sl_byte_builder_init_arena(&builder, arena, 256U, 1024U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_ws_append(&builder, sl_str_from_cstr("HTTP/1.1 101 Switching Protocols\r\n"));
    if (sl_status_is_ok(status)) {
        status = sl_ws_append(&builder, sl_str_from_cstr("Upgrade: websocket\r\n"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_ws_append(&builder, sl_str_from_cstr("Connection: Upgrade\r\n"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_ws_append(&builder, sl_str_from_cstr("Sec-WebSocket-Accept: "));
    }
    if (sl_status_is_ok(status)) {
        status = sl_ws_append(&builder, accept);
    }
    if (sl_status_is_ok(status)) {
        status = sl_ws_append(&builder, sl_str_from_cstr("\r\n"));
    }
    if (sl_status_is_ok(status) && !sl_str_is_empty(protocol)) {
        status = sl_ws_append(&builder, sl_str_from_cstr("Sec-WebSocket-Protocol: "));
        if (sl_status_is_ok(status)) {
            status = sl_ws_append(&builder, protocol);
        }
        if (sl_status_is_ok(status)) {
            status = sl_ws_append(&builder, sl_str_from_cstr("\r\n"));
        }
    }
    if (sl_status_is_ok(status)) {
        status = sl_ws_append(&builder, sl_str_from_cstr("\r\n"));
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_byte_builder_view(&builder);
    return sl_status_ok();
}

SlStatus sl_websocket_build_server_handshake(SlArena* arena, const SlHttpRequestHead* request,
                                             const SlPlanRoute* route,
                                             SlWebSocketHandshakeResult* out_result,
                                             SlDiag* out_diag)
{
    SlStr connection = {0};
    SlStr upgrade = {0};
    SlStr version = {0};
    SlStr key = {0};
    SlStr origin = {0};
    SlStr requested_protocols = {0};
    SlStr accept = {0};
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_result != NULL) {
        *out_result = (SlWebSocketHandshakeResult){0};
    }
    if (arena == NULL || request == NULL || route == NULL || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    out_result->http_status = 400U;
    if (!sl_plan_route_is_websocket(route)) {
        return sl_ws_diag(out_diag, sl_ws_literal("matched route is not a WebSocket route",
                                                  sizeof("matched route is not a WebSocket route") -
                                                      1U));
    }
    if (route->auth.present && route->auth.required && !route->auth.allow_anonymous) {
        out_result->http_status = 401U;
        return sl_ws_diag(out_diag, sl_ws_literal("WebSocket route requires authentication",
                                                  sizeof("WebSocket route requires authentication") -
                                                      1U));
    }
    if (request->method != SL_HTTP_METHOD_GET) {
        out_result->http_status = 405U;
        return sl_ws_diag(out_diag, sl_ws_literal("WebSocket Upgrade requires GET",
                                                  sizeof("WebSocket Upgrade requires GET") - 1U));
    }
    if (request->version_major != 1U || request->version_minor != 1U) {
        return sl_ws_diag(out_diag, sl_ws_literal("WebSocket Upgrade requires HTTP/1.1",
                                                  sizeof("WebSocket Upgrade requires HTTP/1.1") -
                                                      1U));
    }
    if (!sl_ws_header_value(request, sl_str_from_cstr("Connection"), &connection) ||
        !sl_ws_header_has_token(connection, sl_str_from_cstr("Upgrade")) ||
        !sl_ws_header_value(request, sl_str_from_cstr("Upgrade"), &upgrade) ||
        !sl_str_equal_ci_ascii(sl_ws_trim(upgrade), sl_str_from_cstr("websocket")))
    {
        return sl_ws_diag(out_diag, sl_ws_literal("invalid WebSocket Upgrade headers",
                                                  sizeof("invalid WebSocket Upgrade headers") -
                                                      1U));
    }
    if (!sl_ws_header_value(request, sl_str_from_cstr("Sec-WebSocket-Version"), &version) ||
        !sl_str_equal(sl_ws_trim(version), sl_str_from_cstr("13")))
    {
        return sl_ws_diag(out_diag, sl_ws_literal("WebSocket version must be 13",
                                                  sizeof("WebSocket version must be 13") - 1U));
    }
    if (!sl_ws_header_value(request, sl_str_from_cstr("Sec-WebSocket-Key"), &key) ||
        !sl_ws_decode_client_key(sl_ws_trim(key)))
    {
        return sl_ws_diag(out_diag, sl_ws_literal("invalid WebSocket client key",
                                                  sizeof("invalid WebSocket client key") - 1U));
    }
    key = sl_ws_trim(key);
    sl_ws_header_value(request, sl_str_from_cstr("Origin"), &origin);
    if (!sl_ws_origin_allowed(&route->websocket, origin)) {
        out_result->http_status = 403U;
        return sl_ws_diag(out_diag, sl_ws_literal("WebSocket origin is not allowed",
                                                  sizeof("WebSocket origin is not allowed") - 1U));
    }
    sl_ws_header_value(request, sl_str_from_cstr("Sec-WebSocket-Protocol"),
                       &requested_protocols);
    out_result->selected_protocol = sl_ws_select_protocol(&route->websocket, requested_protocols);
    if (route->websocket.protocol_count != 0U && sl_str_is_empty(out_result->selected_protocol)) {
        return sl_ws_diag(
            out_diag,
            sl_ws_literal("WebSocket subprotocol is not allowed",
                          sizeof("WebSocket subprotocol is not allowed") - 1U));
    }
    status = sl_ws_accept_key(arena, key, &accept);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_ws_build_response(arena, accept, out_result->selected_protocol,
                                  &out_result->response_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out_result->http_status = 101U;
    return sl_status_ok();
}

static SlStatus sl_ws_frame_diag(SlDiag* out_diag, SlStatusCode status_code, SlStr message)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = SL_DIAG_INVALID_HTTP_REQUEST;
        out_diag->message = message;
    }
    return sl_status_from_code(status_code);
}

static bool sl_ws_opcode_valid(unsigned char opcode)
{
    return opcode == SL_WEBSOCKET_OPCODE_CONTINUATION || opcode == SL_WEBSOCKET_OPCODE_TEXT ||
           opcode == SL_WEBSOCKET_OPCODE_BINARY || opcode == SL_WEBSOCKET_OPCODE_CLOSE ||
           opcode == SL_WEBSOCKET_OPCODE_PING || opcode == SL_WEBSOCKET_OPCODE_PONG;
}

static bool sl_ws_opcode_control(unsigned char opcode)
{
    return opcode == SL_WEBSOCKET_OPCODE_CLOSE || opcode == SL_WEBSOCKET_OPCODE_PING ||
           opcode == SL_WEBSOCKET_OPCODE_PONG;
}

static uint64_t sl_ws_read_u16(const unsigned char* ptr)
{
    return ((uint64_t)ptr[0] << 8U) | (uint64_t)ptr[1];
}

static uint64_t sl_ws_read_u64(const unsigned char* ptr)
{
    uint64_t value = 0U;

    for (size_t index = 0U; index < 8U; index += 1U) {
        value = (value << 8U) | (uint64_t)ptr[index];
    }
    return value;
}

static bool sl_ws_close_code_valid(uint16_t code)
{
    if (code >= 1000U && code <= 1015U) {
        return code != 1004U && code != 1005U && code != 1006U && code != 1015U;
    }
    return code >= 3000U && code <= 4999U;
}

static bool sl_ws_utf8_valid(SlBytes bytes)
{
    size_t index = 0U;

    if (bytes.ptr == NULL && bytes.length != 0U) {
        return false;
    }
    while (index < bytes.length) {
        unsigned char ch = bytes.ptr[index];
        uint32_t codepoint = 0U;
        size_t needed = 0U;

        if (ch <= 0x7FU) {
            index += 1U;
            continue;
        }
        if (ch >= 0xC2U && ch <= 0xDFU) {
            codepoint = (uint32_t)(ch & 0x1FU);
            needed = 1U;
        }
        else if (ch >= 0xE0U && ch <= 0xEFU) {
            codepoint = (uint32_t)(ch & 0x0FU);
            needed = 2U;
        }
        else if (ch >= 0xF0U && ch <= 0xF4U) {
            codepoint = (uint32_t)(ch & 0x07U);
            needed = 3U;
        }
        else {
            return false;
        }
        if (index + needed >= bytes.length) {
            return false;
        }
        for (size_t offset = 1U; offset <= needed; offset += 1U) {
            unsigned char tail = bytes.ptr[index + offset];
            if ((tail & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (uint32_t)(tail & 0x3FU);
        }
        if ((needed == 2U && codepoint < 0x800U) || (needed == 3U && codepoint < 0x10000U) ||
            codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
        {
            return false;
        }
        index += needed + 1U;
    }
    return true;
}

static SlStatus sl_ws_copy_payload(SlArena* arena, SlBytes payload, bool masked,
                                   const unsigned char mask_key[4], SlBytes* out)
{
    void* storage = NULL;

    if (out == NULL || (payload.ptr == NULL && payload.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = sl_bytes_empty();
    if (payload.length == 0U) {
        return sl_status_ok();
    }
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    SlStatus status = sl_arena_alloc(arena, payload.length, 1U, &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < payload.length; index += 1U) {
        ((unsigned char*)storage)[index] = payload.ptr[index];
    }
    if (masked) {
        unsigned char* bytes = (unsigned char*)storage;
        for (size_t index = 0U; index < payload.length; index += 1U) {
            bytes[index] ^= mask_key[index % 4U];
        }
    }
    *out = sl_bytes_from_parts((const unsigned char*)storage, payload.length);
    return sl_status_ok();
}

SlStatus sl_websocket_parse_frame(SlArena* arena, SlBytes bytes,
                                  const SlWebSocketFrameParseOptions* options,
                                  SlWebSocketFrameParseResult* out_result, SlDiag* out_diag)
{
    SlWebSocketFrameParseOptions effective = {0};
    unsigned char first = 0U;
    unsigned char second = 0U;
    unsigned char opcode = 0U;
    unsigned char mask_key[4] = {0};
    uint64_t payload_length = 0U;
    size_t header_length = 2U;
    bool fin = false;
    bool masked = false;
    SlBytes payload = {0};
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_result != NULL) {
        *out_result = (SlWebSocketFrameParseResult){0};
    }
    if (out_result == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (options != NULL) {
        effective = *options;
    }
    if (effective.max_payload_bytes == 0U) {
        effective.max_payload_bytes = 65536U;
    }
    if (bytes.length < 2U) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    first = bytes.ptr[0];
    second = bytes.ptr[1];
    fin = (first & 0x80U) != 0U;
    opcode = first & 0x0FU;
    masked = (second & 0x80U) != 0U;
    payload_length = (uint64_t)(second & 0x7FU);
    out_result->close_code = 1002U;

    if ((first & 0x70U) != 0U) {
        return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                sl_ws_literal("WebSocket RSV bits are not supported",
                                              sizeof("WebSocket RSV bits are not supported") - 1U));
    }
    if (!sl_ws_opcode_valid(opcode)) {
        return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                sl_ws_literal("WebSocket frame opcode is invalid",
                                              sizeof("WebSocket frame opcode is invalid") - 1U));
    }
    if (effective.require_mask && !masked) {
        return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                sl_ws_literal("client WebSocket frames must be masked",
                                              sizeof("client WebSocket frames must be masked") -
                                                  1U));
    }
    if (sl_ws_opcode_control(opcode) && !fin) {
        return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                sl_ws_literal("WebSocket control frames must not be fragmented",
                                              sizeof("WebSocket control frames must not be "
                                                     "fragmented") -
                                                  1U));
    }
    if (payload_length == 126U) {
        if (bytes.length < header_length + 2U) {
            return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
        }
        payload_length = sl_ws_read_u16(bytes.ptr + header_length);
        header_length += 2U;
        if (payload_length <= 125U) {
            return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                    sl_ws_literal("WebSocket frame length is not minimally encoded",
                                                  sizeof("WebSocket frame length is not minimally "
                                                         "encoded") -
                                                      1U));
        }
    }
    else if (payload_length == 127U) {
        if (bytes.length < header_length + 8U) {
            return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
        }
        if ((bytes.ptr[header_length] & 0x80U) != 0U) {
            return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                    sl_ws_literal("WebSocket frame length exceeds signed bounds",
                                                  sizeof("WebSocket frame length exceeds signed "
                                                         "bounds") -
                                                      1U));
        }
        payload_length = sl_ws_read_u64(bytes.ptr + header_length);
        header_length += 8U;
        if (payload_length <= 65535U) {
            return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                    sl_ws_literal("WebSocket frame length is not minimally encoded",
                                                  sizeof("WebSocket frame length is not minimally "
                                                         "encoded") -
                                                      1U));
        }
    }
    if (sl_ws_opcode_control(opcode) && payload_length > 125U) {
        return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                sl_ws_literal("WebSocket control frame payload is too large",
                                              sizeof("WebSocket control frame payload is too "
                                                     "large") -
                                                  1U));
    }
    if (payload_length > (uint64_t)effective.max_payload_bytes ||
        payload_length > (uint64_t)SIZE_MAX)
    {
        out_result->close_code = 1009U;
        return sl_ws_frame_diag(out_diag, SL_STATUS_CAPACITY_EXCEEDED,
                                sl_ws_literal("WebSocket frame payload is too large",
                                              sizeof("WebSocket frame payload is too large") -
                                                  1U));
    }
    if (masked) {
        if (bytes.length < header_length + 4U) {
            return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
        }
        for (size_t index = 0U; index < sizeof(mask_key); index += 1U) {
            mask_key[index] = bytes.ptr[header_length + index];
        }
        header_length += 4U;
    }
    if (bytes.length < header_length + (size_t)payload_length) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    payload = sl_bytes_from_parts(bytes.ptr + header_length, (size_t)payload_length);
    status = sl_ws_copy_payload(arena, payload, masked, mask_key, &payload);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (opcode == SL_WEBSOCKET_OPCODE_CLOSE) {
        if (payload.length == 1U) {
            return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                    sl_ws_literal("WebSocket close payload is malformed",
                                                  sizeof("WebSocket close payload is malformed") -
                                                      1U));
        }
        if (payload.length >= 2U) {
            uint16_t close_code = (uint16_t)(((uint16_t)payload.ptr[0] << 8U) | payload.ptr[1]);
            if (!sl_ws_close_code_valid(close_code)) {
                return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                        sl_ws_literal("WebSocket close code is invalid",
                                                      sizeof("WebSocket close code is invalid") -
                                                          1U));
            }
            if (!sl_ws_utf8_valid(sl_bytes_from_parts(payload.ptr + 2U, payload.length - 2U))) {
                out_result->close_code = 1007U;
                return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                        sl_ws_literal("WebSocket close reason is not valid UTF-8",
                                                      sizeof("WebSocket close reason is not valid "
                                                             "UTF-8") -
                                                          1U));
            }
        }
    }
    if (opcode == SL_WEBSOCKET_OPCODE_TEXT && fin && !sl_ws_utf8_valid(payload)) {
        out_result->close_code = 1007U;
        return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                sl_ws_literal("WebSocket text payload is not valid UTF-8",
                                              sizeof("WebSocket text payload is not valid UTF-8") -
                                                  1U));
    }

    out_result->frame.fin = fin;
    out_result->frame.masked = masked;
    out_result->frame.opcode = (SlWebSocketOpcode)opcode;
    out_result->frame.payload = payload;
    out_result->consumed = header_length + (size_t)payload_length;
    out_result->close_code = opcode == SL_WEBSOCKET_OPCODE_CLOSE && payload.length >= 2U
                                 ? (uint16_t)(((uint16_t)payload.ptr[0] << 8U) | payload.ptr[1])
                                 : 0U;
    return sl_status_ok();
}

static SlStatus sl_ws_write_u16(SlByteBuilder* builder, uint64_t value)
{
    SlStatus status = sl_byte_builder_append_byte(builder, (unsigned char)((value >> 8U) & 0xFFU));
    if (sl_status_is_ok(status)) {
        status = sl_byte_builder_append_byte(builder, (unsigned char)(value & 0xFFU));
    }
    return status;
}

static SlStatus sl_ws_write_u64(SlByteBuilder* builder, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        SlStatus status =
            sl_byte_builder_append_byte(builder, (unsigned char)((value >> (unsigned)shift) & 0xFFU));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

SlStatus sl_websocket_write_frame(SlByteBuilder* builder,
                                  const SlWebSocketFrameWriteOptions* options, SlDiag* out_diag)
{
    SlBytes payload = {0};
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (builder == NULL || options == NULL ||
        (options->payload.ptr == NULL && options->payload.length != 0U))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_ws_opcode_valid((unsigned char)options->opcode)) {
        return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                sl_ws_literal("WebSocket frame opcode is invalid",
                                              sizeof("WebSocket frame opcode is invalid") - 1U));
    }
    if (sl_ws_opcode_control((unsigned char)options->opcode)) {
        if (!options->fin) {
            return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                    sl_ws_literal("WebSocket control frames must not be fragmented",
                                                  sizeof("WebSocket control frames must not be "
                                                         "fragmented") -
                                                      1U));
        }
        if (options->payload.length > 125U) {
            return sl_ws_frame_diag(out_diag, SL_STATUS_INVALID_ARGUMENT,
                                    sl_ws_literal("WebSocket control frame payload is too large",
                                                  sizeof("WebSocket control frame payload is too "
                                                         "large") -
                                                      1U));
        }
    }

    payload = options->payload;
    status = sl_byte_builder_append_byte(
        builder, (unsigned char)((options->fin ? 0x80U : 0U) | ((unsigned char)options->opcode)));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (payload.length <= 125U) {
        status = sl_byte_builder_append_byte(
            builder, (unsigned char)((options->mask ? 0x80U : 0U) | payload.length));
    }
    else if (payload.length <= 65535U) {
        status = sl_byte_builder_append_byte(builder,
                                             (unsigned char)((options->mask ? 0x80U : 0U) | 126U));
        if (sl_status_is_ok(status)) {
            status = sl_ws_write_u16(builder, payload.length);
        }
    }
    else {
        status = sl_byte_builder_append_byte(builder,
                                             (unsigned char)((options->mask ? 0x80U : 0U) | 127U));
        if (sl_status_is_ok(status)) {
            status = sl_ws_write_u64(builder, (uint64_t)payload.length);
        }
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (options->mask) {
        status =
            sl_byte_builder_append_bytes(builder, sl_bytes_from_parts(options->mask_key, 4U));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        for (size_t index = 0U; index < payload.length; index += 1U) {
            status = sl_byte_builder_append_byte(
                builder, (unsigned char)(payload.ptr[index] ^ options->mask_key[index % 4U]));
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        return sl_status_ok();
    }
    return sl_byte_builder_append_bytes(builder, payload);
}
