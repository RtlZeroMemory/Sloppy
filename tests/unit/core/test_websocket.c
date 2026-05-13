#include "sloppy/websocket.h"

#include <stdio.h>
#include <string.h>

#define TEST_ARENA_SIZE 16384U

static bool bytes_contains(SlBytes bytes, const char* needle)
{
    size_t needle_length = sl_str_from_cstr(needle).length;

    if (bytes.ptr == NULL || needle == NULL || needle_length == 0U || needle_length > bytes.length)
    {
        return false;
    }
    for (size_t index = 0U; index + needle_length <= bytes.length; index += 1U) {
        if (memcmp(bytes.ptr + index, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static int expect_status(SlStatus status, SlStatusCode expected)
{
    return sl_status_code(status) == expected ? 0 : 1;
}

static SlBytes bytes_from_cstr(const char* value)
{
    SlStr str = sl_str_from_cstr(value);
    return sl_bytes_from_parts((const unsigned char*)str.ptr, str.length);
}

static SlHttpHeader header(const char* name, const char* value)
{
    SlHttpHeader result = {0};
    result.name = sl_str_from_cstr(name);
    result.value = sl_str_from_cstr(value);
    return result;
}

static SlHttpRequestHead websocket_request(SlHttpHeader* headers, size_t header_count)
{
    SlHttpRequestHead request = {0};
    request.method = SL_HTTP_METHOD_GET;
    request.version_major = 1U;
    request.version_minor = 1U;
    request.path = sl_str_from_cstr("/ws");
    request.raw_target = sl_str_from_cstr("/ws");
    request.headers = headers;
    request.header_count = header_count;
    return request;
}

static SlPlanRoute websocket_route(const SlStr* protocols, size_t protocol_count,
                                   const SlStr* origins, size_t origin_count)
{
    SlPlanRoute route = {0};
    route.kind = sl_str_from_cstr("websocket");
    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/ws");
    route.handler_id = 1U;
    route.websocket.protocols = protocols;
    route.websocket.protocol_count = protocol_count;
    route.websocket.origins = origins;
    route.websocket.origin_count = origin_count;
    route.websocket.origin_policy =
        origin_count == 0U ? SL_PLAN_WEBSOCKET_ORIGINS_NONE : SL_PLAN_WEBSOCKET_ORIGINS_LIST;
    route.websocket.max_message_bytes = 65536U;
    route.websocket.max_send_queue_bytes = 1048576U;
    route.websocket.close_timeout_ms = 5000U;
    return route;
}

static int test_valid_handshake_selects_protocol_and_builds_101(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlDiag diag = {0};
    SlWebSocketHandshakeResult result = {0};
    SlStr protocols[] = {sl_str_from_cstr("sloppy.realtime")};
    SlStr origins[] = {sl_str_from_cstr("https://app.example.com")};
    SlPlanRoute route = websocket_route(protocols, 1U, origins, 1U);
    SlHttpHeader headers[] = {
        header("Host", "example.test"),
        header("Connection", "keep-alive, Upgrade"),
        header("Upgrade", "websocket"),
        header("Sec-WebSocket-Version", "13"),
        header("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="),
        header("Origin", "https://app.example.com"),
        header("Sec-WebSocket-Protocol", "other, sloppy.realtime"),
    };
    SlHttpRequestHead request = websocket_request(headers, sizeof(headers) / sizeof(headers[0]));
    SlStatus status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));

    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_websocket_build_server_handshake(&arena, &request, &route, &result, &diag);
    if (!sl_status_is_ok(status) || diag.code != SL_DIAG_NONE || result.http_status != 101U) {
        return 2;
    }
    if (!sl_str_equal(result.selected_protocol, sl_str_from_cstr("sloppy.realtime"))) {
        return 3;
    }
    if (result.response_bytes.ptr == NULL ||
        !bytes_contains(result.response_bytes, "HTTP/1.1 101 Switching Protocols") ||
        !bytes_contains(result.response_bytes,
                        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") ||
        !bytes_contains(result.response_bytes, "Sec-WebSocket-Protocol: sloppy.realtime"))
    {
        return 4;
    }
    return 0;
}

static int test_invalid_handshake_inputs_are_rejected_before_101(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlDiag diag = {0};
    SlWebSocketHandshakeResult result = {0};
    SlStr origins[] = {sl_str_from_cstr("https://app.example.com")};
    SlPlanRoute route = websocket_route(NULL, 0U, origins, 1U);
    SlHttpHeader bad_origin_headers[] = {
        header("Host", "example.test"),
        header("Connection", "Upgrade"),
        header("Upgrade", "websocket"),
        header("Sec-WebSocket-Version", "13"),
        header("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="),
        header("Origin", "https://blocked.example.com"),
    };
    SlHttpRequestHead request = websocket_request(
        bad_origin_headers, sizeof(bad_origin_headers) / sizeof(bad_origin_headers[0]));
    SlStatus status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));

    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_websocket_build_server_handshake(&arena, &request, &route, &result, &diag);
    if (!sl_status_is_ok(status) || result.http_status != 403U ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 2;
    }

    diag = (SlDiag){0};
    result = (SlWebSocketHandshakeResult){0};
    bad_origin_headers[4] = header("Sec-WebSocket-Key", "bad-key");
    bad_origin_headers[5] = header("Origin", "https://app.example.com");
    status = sl_websocket_build_server_handshake(&arena, &request, &route, &result, &diag);
    if (!sl_status_is_ok(status) || result.http_status != 400U ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 3;
    }

    diag = (SlDiag){0};
    result = (SlWebSocketHandshakeResult){0};
    bad_origin_headers[4] = header("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    route.auth.present = true;
    route.auth.required = true;
    route.auth.allow_anonymous = false;
    status = sl_websocket_build_server_handshake(&arena, &request, &route, &result, &diag);
    if (!sl_status_is_ok(status) || result.http_status != 401U ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 4;
    }
    return 0;
}

static int test_protocol_requirement_rejects_missing_match(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlDiag diag = {0};
    SlWebSocketHandshakeResult result = {0};
    SlStr protocols[] = {sl_str_from_cstr("sloppy.realtime")};
    SlPlanRoute route = websocket_route(protocols, 1U, NULL, 0U);
    SlHttpHeader headers[] = {
        header("Host", "example.test"),
        header("Connection", "Upgrade"),
        header("Upgrade", "websocket"),
        header("Sec-WebSocket-Version", "13"),
        header("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="),
        header("Sec-WebSocket-Protocol", "chat"),
    };
    SlHttpRequestHead request = websocket_request(headers, sizeof(headers) / sizeof(headers[0]));
    SlStatus status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));

    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_websocket_build_server_handshake(&arena, &request, &route, &result, &diag);
    if (!sl_status_is_ok(status) || result.http_status != 400U ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 2;
    }
    return 0;
}

static int test_frame_writer_outputs_unmasked_server_text(void)
{
    unsigned char storage[64];
    SlByteBuilder builder = {0};
    SlWebSocketFrameWriteOptions options = {0};
    SlDiag diag = {0};
    const unsigned char expected[] = {0x81U, 0x05U, 'h', 'e', 'l', 'l', 'o'};
    SlStatus status = sl_byte_builder_init_fixed(&builder, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 1;
    }
    options.fin = true;
    options.opcode = SL_WEBSOCKET_OPCODE_TEXT;
    options.payload = bytes_from_cstr("hello");
    status = sl_websocket_write_frame(&builder, &options, &diag);
    if (!sl_status_is_ok(status) || diag.code != SL_DIAG_NONE ||
        !sl_bytes_equal(sl_byte_builder_view(&builder),
                        sl_bytes_from_parts(expected, sizeof(expected))))
    {
        return 2;
    }
    return 0;
}

static int test_frame_parser_unmasks_client_text(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    unsigned char frame_storage[64];
    SlArena arena = {0};
    SlByteBuilder builder = {0};
    SlWebSocketFrameWriteOptions write = {0};
    SlWebSocketFrameParseOptions parse = {.require_mask = true, .max_payload_bytes = 1024U};
    SlWebSocketFrameParseResult result = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));

    if (!sl_status_is_ok(status) || !sl_status_is_ok(sl_byte_builder_init_fixed(
                                        &builder, frame_storage, sizeof(frame_storage))))
    {
        return 1;
    }
    write.fin = true;
    write.mask = true;
    write.opcode = SL_WEBSOCKET_OPCODE_TEXT;
    write.payload = bytes_from_cstr("hello");
    write.mask_key[0] = 0x37U;
    write.mask_key[1] = 0xfaU;
    write.mask_key[2] = 0x21U;
    write.mask_key[3] = 0x3dU;
    status = sl_websocket_write_frame(&builder, &write, &diag);
    if (!sl_status_is_ok(status)) {
        return 2;
    }
    status =
        sl_websocket_parse_frame(&arena, sl_byte_builder_view(&builder), &parse, &result, &diag);
    if (!sl_status_is_ok(status) || diag.code != SL_DIAG_NONE ||
        result.frame.opcode != SL_WEBSOCKET_OPCODE_TEXT || !result.frame.fin ||
        !result.frame.masked || result.consumed != sl_byte_builder_length(&builder) ||
        !sl_bytes_equal(result.frame.payload, bytes_from_cstr("hello")))
    {
        return 3;
    }
    return 0;
}

static int test_frame_parser_rejects_unmasked_client_frame(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlWebSocketFrameParseOptions parse = {.require_mask = true, .max_payload_bytes = 1024U};
    SlWebSocketFrameParseResult result = {0};
    SlDiag diag = {0};
    const unsigned char frame[] = {0x81U, 0x02U, 'o', 'k'};

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 1;
    }
    if (expect_status(sl_websocket_parse_frame(&arena, sl_bytes_from_parts(frame, sizeof(frame)),
                                               &parse, &result, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        result.close_code != 1002U || diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 2;
    }
    return 0;
}

static int test_frame_parser_rejects_protocol_errors(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlWebSocketFrameParseOptions parse = {.require_mask = false, .max_payload_bytes = 1024U};
    SlWebSocketFrameParseResult result = {0};
    SlDiag diag = {0};
    const unsigned char bad_opcode[] = {0x83U, 0x00U};
    const unsigned char fragmented_ping[] = {0x09U, 0x00U};
    const unsigned char close_one_byte[] = {0x88U, 0x01U, 0x03U};
    const unsigned char close_bad_code[] = {0x88U, 0x02U, 0x03U, 0xEEU};

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 1;
    }
    if (expect_status(sl_websocket_parse_frame(&arena,
                                               sl_bytes_from_parts(bad_opcode, sizeof(bad_opcode)),
                                               &parse, &result, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 2;
    }
    if (expect_status(sl_websocket_parse_frame(
                          &arena, sl_bytes_from_parts(fragmented_ping, sizeof(fragmented_ping)),
                          &parse, &result, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 3;
    }
    if (expect_status(sl_websocket_parse_frame(
                          &arena, sl_bytes_from_parts(close_one_byte, sizeof(close_one_byte)),
                          &parse, &result, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 4;
    }
    if (expect_status(sl_websocket_parse_frame(
                          &arena, sl_bytes_from_parts(close_bad_code, sizeof(close_bad_code)),
                          &parse, &result, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 5;
    }
    return 0;
}

static int test_frame_parser_enforces_payload_limit(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlWebSocketFrameParseOptions parse = {.require_mask = false, .max_payload_bytes = 4U};
    SlWebSocketFrameParseResult result = {0};
    SlDiag diag = {0};
    const unsigned char frame[] = {0x82U, 0x05U, 1U, 2U, 3U, 4U, 5U};

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 1;
    }
    if (expect_status(sl_websocket_parse_frame(&arena, sl_bytes_from_parts(frame, sizeof(frame)),
                                               &parse, &result, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        result.close_code != 1009U)
    {
        return 2;
    }
    return 0;
}

int main(void)
{
    int result = test_valid_handshake_selects_protocol_and_builds_101();
    if (result != 0) {
        fprintf(stderr, "test_valid_handshake_selects_protocol_and_builds_101 failed: %d\n",
                result);
        return result;
    }
    result = test_invalid_handshake_inputs_are_rejected_before_101();
    if (result != 0) {
        fprintf(stderr, "test_invalid_handshake_inputs_are_rejected_before_101 failed: %d\n",
                result);
        return result;
    }
    result = test_protocol_requirement_rejects_missing_match();
    if (result != 0) {
        fprintf(stderr, "test_protocol_requirement_rejects_missing_match failed: %d\n", result);
        return result;
    }
    result = test_frame_writer_outputs_unmasked_server_text();
    if (result != 0) {
        fprintf(stderr, "test_frame_writer_outputs_unmasked_server_text failed: %d\n", result);
        return result;
    }
    result = test_frame_parser_unmasks_client_text();
    if (result != 0) {
        fprintf(stderr, "test_frame_parser_unmasks_client_text failed: %d\n", result);
        return result;
    }
    result = test_frame_parser_rejects_unmasked_client_frame();
    if (result != 0) {
        fprintf(stderr, "test_frame_parser_rejects_unmasked_client_frame failed: %d\n", result);
        return result;
    }
    result = test_frame_parser_rejects_protocol_errors();
    if (result != 0) {
        fprintf(stderr, "test_frame_parser_rejects_protocol_errors failed: %d\n", result);
        return result;
    }
    result = test_frame_parser_enforces_payload_limit();
    if (result != 0) {
        fprintf(stderr, "test_frame_parser_enforces_payload_limit failed: %d\n", result);
        return result;
    }
    return 0;
}
