#include "sloppy/http2_dispatch.h"

#include <stdbool.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlBytes bytes_from_cstr(const char* text)
{
    SlStr str = sl_str_from_cstr(text);
    return sl_bytes_from_parts((const unsigned char*)str.ptr, str.length);
}

static SlHttp2HeaderField h2_header(const char* name, const char* value)
{
    return (SlHttp2HeaderField){
        .name = sl_str_from_cstr(name), .value = sl_str_from_cstr(value), .sensitive = false};
}

typedef struct DispatchHook
{
    size_t count;
    SlHttpMethod methods[64];
    SlStr paths[64];
    SlBytes bodies[64];
    const char* response_bodies[64];
    SlStatusCode status_code;
    SlDiagCode diag_code;
} DispatchHook;

static SlStatus dispatch_hook(SlHttpConnection* connection, SlArena* arena,
                              const SlHttpRequestLifecycle* request, SlHttpResponse* out_response,
                              SlDiag* out_diag, void* user)
{
    DispatchHook* hook = (DispatchHook*)user;
    size_t index = 0U;

    (void)connection;
    (void)arena;
    if (hook == NULL || request == NULL || out_response == NULL ||
        hook->count >= sizeof(hook->paths) / sizeof(hook->paths[0]))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    index = hook->count;
    hook->methods[index] = request->head.method;
    hook->paths[index] = request->head.path;
    hook->bodies[index] = request->head.body;
    hook->count += 1U;

    if (hook->status_code != SL_STATUS_OK) {
        if (out_diag != NULL) {
            *out_diag = (SlDiag){0};
            out_diag->severity = SL_DIAG_SEVERITY_ERROR;
            out_diag->code = hook->diag_code;
            out_diag->message = sl_str_from_cstr("test dispatch failure");
        }
        return sl_status_from_code(hook->status_code);
    }

    if (hook->response_bodies[index] != NULL) {
        *out_response = sl_http_response_text(200U, sl_str_from_cstr(hook->response_bodies[index]));
    }
    else if (sl_str_equal(request->head.path, sl_str_from_cstr("/one"))) {
        *out_response = sl_http_response_text(200U, sl_str_from_cstr("first\n"));
    }
    else if (sl_str_equal(request->head.path, sl_str_from_cstr("/two"))) {
        *out_response = sl_http_response_text(200U, sl_str_from_cstr("second\n"));
    }
    else {
        *out_response = sl_http_response_text(200U, sl_str_from_cstr("ok\n"));
    }
    return sl_status_ok();
}

static int pump_client_to_server(SlHttp2Session* client, SlHttp2ServerDispatcher* server)
{
    SlBytes bytes = {0};
    size_t consumed = 0U;

    if (expect_status(sl_http2_session_drain_output(client, &bytes), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (bytes.length == 0U) {
        return 0;
    }
    if (expect_status(sl_http2_server_dispatcher_receive(server, bytes, &consumed), SL_STATUS_OK) !=
            0 ||
        consumed != bytes.length)
    {
        return 2;
    }
    return 0;
}

static int pump_server_to_client(SlHttp2ServerDispatcher* server, SlHttp2Session* client)
{
    SlBytes bytes = {0};
    size_t consumed = 0U;

    if (expect_status(sl_http2_server_dispatcher_drain_output(server, &bytes), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (bytes.length == 0U) {
        return 0;
    }
    if (expect_status(sl_http2_session_receive(client, bytes, &consumed), SL_STATUS_OK) != 0 ||
        consumed != bytes.length)
    {
        return 2;
    }
    return 0;
}

static int receive_server_bytes(SlHttp2ServerDispatcher* server, SlBytes bytes)
{
    size_t consumed = 0U;

    if (expect_status(sl_http2_server_dispatcher_receive(server, bytes, &consumed), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }
    return expect_true(consumed == bytes.length);
}

static size_t first_frame_size(SlBytes bytes)
{
    if (bytes.ptr == NULL || bytes.length < 9U) {
        return 0U;
    }
    return 9U + ((size_t)bytes.ptr[0] << 16U) + ((size_t)bytes.ptr[1] << 8U) + (size_t)bytes.ptr[2];
}

static bool frame_bytes_contain_type(SlBytes bytes, uint8_t type)
{
    size_t offset = 0U;

    while (bytes.ptr != NULL && offset + 9U <= bytes.length) {
        size_t length = ((size_t)bytes.ptr[offset] << 16U) |
                        ((size_t)bytes.ptr[offset + 1U] << 8U) | (size_t)bytes.ptr[offset + 2U];
        if (bytes.ptr[offset + 3U] == type) {
            return true;
        }
        if (length > bytes.length - offset - 9U) {
            return false;
        }
        offset += 9U + length;
    }
    return false;
}

static int init_h2_pair(SlArena* client_arena, SlArena* server_arena, SlHttpBackend* backend,
                        SlHttpConnection* connection, SlHttp2Session* client,
                        SlHttp2ServerDispatcher* server, DispatchHook* hook)
{
    SlHttpBackendOptions backend_options = {0};
    SlHttp2SessionConfig client_config = {.role = SL_HTTP2_SESSION_ROLE_CLIENT};
    SlHttp2DispatchConfig dispatch_config = {0};

    backend_options.max_connections = 1U;
    backend_options.max_active_requests = 4U;
    backend_options.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    backend_options.keep_alive_enabled = false;
    dispatch_config.dispatch = dispatch_hook;
    dispatch_config.dispatch_user = hook;
    dispatch_config.max_streams = 4U;
    dispatch_config.max_body_bytes = 128U;
    dispatch_config.max_response_body_bytes = 1024U;

    if (expect_status(sl_http_backend_init(backend, &backend_options, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(backend, connection, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_session_init(client, client_arena, &client_config), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http2_server_dispatcher_init(server, server_arena, connection, &dispatch_config),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    connection->scheme = sl_str_from_cstr("https");

    if (pump_client_to_server(client, server) != 0 || pump_server_to_client(server, client) != 0 ||
        pump_client_to_server(client, server) != 0)
    {
        return 2;
    }

    sl_http2_session_clear_events(client);
    sl_http2_session_clear_events(&server->session);
    return 0;
}

static int send_header_block_frame(SlHttp2ServerDispatcher* server, int32_t stream_id,
                                   uint8_t flags, const SlHttp2HeaderList* headers)
{
    unsigned char arena_storage[8192];
    unsigned char frame_storage[8192];
    SlArena arena = {0};
    SlHttp2HpackEncoder encoder = {0};
    SlBytes header_block = {0};
    SlHttp2Frame frame = {0};
    SlBytes frame_bytes = {0};
    int result = 0;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_hpack_encoder_init(&encoder, 0U, sizeof(frame_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_hpack_encode(&encoder, &arena, headers, &header_block),
                      SL_STATUS_OK) != 0)
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        return 1;
    }

    frame.header.length = (uint32_t)header_block.length;
    frame.header.type = SL_HTTP2_FRAME_HEADERS;
    frame.header.flags = flags;
    frame.header.stream_id = (uint32_t)stream_id;
    frame.payload = header_block;
    result = expect_status(
        sl_http2_frame_write(&frame, frame_storage, sizeof(frame_storage), &frame_bytes),
        SL_STATUS_OK);
    if (result == 0) {
        result = receive_server_bytes(server, frame_bytes);
    }

    sl_http2_hpack_encoder_dispose(&encoder);
    return result;
}

static bool event_header_equals(const SlHttp2Event* event, const char* name, const char* value)
{
    if (event == NULL) {
        return false;
    }
    for (size_t index = 0U; index < event->headers.count; index += 1U) {
        const SlHttp2HeaderField* field = &event->headers.fields[index];
        if (sl_str_equal(field->name, sl_str_from_cstr(name)) &&
            sl_str_equal(field->value, sl_str_from_cstr(value)))
        {
            return true;
        }
    }
    return false;
}

static bool saw_response_with_body(const SlHttp2EventList* events, int32_t stream_id,
                                   const char* status, const char* body)
{
    bool saw_headers = false;
    bool saw_body = false;

    if (events == NULL) {
        return false;
    }
    for (size_t index = 0U; index < events->count; index += 1U) {
        const SlHttp2Event* event = &events->events[index];
        if (event->stream_id != stream_id) {
            continue;
        }
        if (event->type == SL_HTTP2_EVENT_RESPONSE_HEADERS &&
            event_header_equals(event, ":status", status))
        {
            saw_headers = true;
        }
        if (event->type == SL_HTTP2_EVENT_DATA &&
            sl_bytes_equal(event->data, bytes_from_cstr(body)))
        {
            saw_body = true;
        }
    }
    return saw_headers && saw_body;
}

static bool saw_rst_stream(const SlHttp2EventList* events, int32_t stream_id, uint32_t error_code)
{
    if (events == NULL) {
        return false;
    }
    for (size_t index = 0U; index < events->count; index += 1U) {
        const SlHttp2Event* event = &events->events[index];
        if (event->type == SL_HTTP2_EVENT_RST_STREAM && event->stream_id == stream_id &&
            event->error_code == error_code)
        {
            return true;
        }
    }
    return false;
}

static const SlHttp2Event* find_client_event(const SlHttp2EventList* events, SlHttp2EventType type,
                                             int32_t stream_id)
{
    if (events == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < events->count; index += 1U) {
        const SlHttp2Event* event = &events->events[index];
        if (event->type == type && event->stream_id == stream_id) {
            return event;
        }
    }
    return NULL;
}

static bool hook_saw_request(const DispatchHook* hook, SlHttpMethod method, const char* path,
                             const char* body)
{
    if (hook == NULL) {
        return false;
    }
    for (size_t index = 0U; index < hook->count; index += 1U) {
        if (hook->methods[index] == method &&
            sl_str_equal(hook->paths[index], sl_str_from_cstr(path)) &&
            sl_bytes_equal(hook->bodies[index], bytes_from_cstr(body)))
        {
            return true;
        }
    }
    return false;
}

static int test_dispatcher_handles_multiple_streams_on_one_connection(void)
{
    unsigned char client_storage[131072];
    unsigned char server_storage[131072];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField first_fields[] = {h2_header(":method", "POST"),
                                         h2_header(":scheme", "https"),
                                         h2_header(":authority", "localhost"),
                                         h2_header(":path", "/one"),
                                         h2_header("content-type", "text/plain"),
                                         h2_header("content-length", "5")};
    SlHttp2HeaderField second_fields[] = {
        h2_header(":method", "GET"), h2_header(":scheme", "https"),
        h2_header(":authority", "localhost"), h2_header(":path", "/two")};
    SlHttp2HeaderList first_headers = {.fields = first_fields,
                                       .count = sizeof(first_fields) / sizeof(first_fields[0])};
    SlHttp2HeaderList second_headers = {.fields = second_fields,
                                        .count = sizeof(second_fields) / sizeof(second_fields[0])};
    SlHttp2EventList events = {0};
    int32_t first_stream = 0;
    int32_t second_stream = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_session_submit_request(&client, &first_headers,
                                                      bytes_from_cstr("first"), &first_stream),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_session_submit_request(&client, &second_headers, sl_bytes_empty(),
                                                      &second_stream),
                      SL_STATUS_OK) != 0 ||
        first_stream != 1 || second_stream != 3 || pump_client_to_server(&client, &server) != 0)
    {
        return 2;
    }

    if (hook.count != 2U || !hook_saw_request(&hook, SL_HTTP_METHOD_POST, "/one", "first") ||
        !hook_saw_request(&hook, SL_HTTP_METHOD_GET, "/two", "") || backend.active_requests != 0U ||
        connection.request_count != 2U ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_OPEN ||
        sl_http2_server_dispatcher_active_streams(&server) != 0U)
    {
        return 3;
    }

    if (pump_server_to_client(&server, &client) != 0) {
        return 4;
    }
    events = sl_http2_session_events(&client);
    if (!saw_response_with_body(&events, first_stream, "200", "first\n") ||
        !saw_response_with_body(&events, second_stream, "200", "second\n"))
    {
        return 5;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_one_bad_stream_does_not_block_a_good_stream(void)
{
    unsigned char client_storage[131072];
    unsigned char server_storage[131072];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField bad_fields[] = {h2_header(":method", "GET"), h2_header(":scheme", "https"),
                                       h2_header(":authority", "localhost"),
                                       h2_header(":path", "/bad"),
                                       h2_header("connection", "close")};
    SlHttp2HeaderField good_fields[] = {h2_header(":method", "GET"), h2_header(":scheme", "https"),
                                        h2_header(":authority", "localhost"),
                                        h2_header(":path", "/good")};
    SlHttp2HeaderList bad_headers = {.fields = bad_fields,
                                     .count = sizeof(bad_fields) / sizeof(bad_fields[0])};
    SlHttp2HeaderList good_headers = {.fields = good_fields,
                                      .count = sizeof(good_fields) / sizeof(good_fields[0])};
    SlHttp2EventList events = {0};
    int32_t bad_stream = 0;
    int32_t good_stream = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 46;
    }

    if (expect_status(
            sl_http2_session_submit_request(&client, &bad_headers, sl_bytes_empty(), &bad_stream),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_http2_session_submit_request(&client, &good_headers, sl_bytes_empty(), &good_stream),
            SL_STATUS_OK) != 0 ||
        pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 47;
    }

    events = sl_http2_session_events(&client);
    if (hook.count != 1U || !hook_saw_request(&hook, SL_HTTP_METHOD_GET, "/good", "") ||
        !saw_rst_stream(&events, bad_stream, SL_HTTP2_ERROR_PROTOCOL_ERROR) ||
        !saw_response_with_body(&events, good_stream, "200", "ok\n"))
    {
        return 48;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_head_request_suppresses_body_but_keeps_content_length(void)
{
    unsigned char client_storage[65536];
    unsigned char server_storage[65536];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField fields[] = {h2_header(":method", "HEAD"), h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"),
                                   h2_header(":path", "/head-ok")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlHttp2EventList events = {0};
    const SlHttp2Event* response_headers = NULL;
    int32_t stream_id = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 40;
    }

    if (expect_status(
            sl_http2_session_submit_request(&client, &headers, sl_bytes_empty(), &stream_id),
            SL_STATUS_OK) != 0 ||
        pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 41;
    }

    events = sl_http2_session_events(&client);
    response_headers = find_client_event(&events, SL_HTTP2_EVENT_RESPONSE_HEADERS, stream_id);
    if (hook.count != 1U || !hook_saw_request(&hook, SL_HTTP_METHOD_HEAD, "/head-ok", "") ||
        response_headers == NULL || !event_header_equals(response_headers, ":status", "200") ||
        !event_header_equals(response_headers, "content-length", "3") ||
        find_client_event(&events, SL_HTTP2_EVENT_DATA, stream_id) != NULL)
    {
        return 42;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_head_dispatch_failure_maps_405_without_body(void)
{
    unsigned char client_storage[65536];
    unsigned char server_storage[65536];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {.status_code = SL_STATUS_UNSUPPORTED,
                         .diag_code = SL_DIAG_HTTP_UNSUPPORTED_METHOD};
    SlHttp2HeaderField fields[] = {h2_header(":method", "HEAD"), h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"),
                                   h2_header(":path", "/head-405")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlHttp2EventList events = {0};
    const SlHttp2Event* response_headers = NULL;
    int32_t stream_id = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 43;
    }

    if (expect_status(
            sl_http2_session_submit_request(&client, &headers, sl_bytes_empty(), &stream_id),
            SL_STATUS_OK) != 0 ||
        pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 44;
    }

    events = sl_http2_session_events(&client);
    response_headers = find_client_event(&events, SL_HTTP2_EVENT_RESPONSE_HEADERS, stream_id);
    if (hook.count != 1U || !hook_saw_request(&hook, SL_HTTP_METHOD_HEAD, "/head-405", "") ||
        response_headers == NULL || !event_header_equals(response_headers, ":status", "405") ||
        !event_header_equals(response_headers, "content-length", "19") ||
        find_client_event(&events, SL_HTTP2_EVENT_DATA, stream_id) != NULL)
    {
        return 45;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_active_stream_cap_resets_new_streams_until_existing_stream_completes(void)
{
    unsigned char client_storage[131072];
    unsigned char server_storage[131072];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2DispatchConfig config = {0};
    SlHttp2HeaderField first_fields[] = {h2_header(":method", "POST"),
                                         h2_header(":scheme", "https"),
                                         h2_header(":authority", "localhost"),
                                         h2_header(":path", "/hold"),
                                         h2_header("content-type", "text/plain"),
                                         h2_header("content-length", "1")};
    SlHttp2HeaderField second_fields[] = {
        h2_header(":method", "GET"), h2_header(":scheme", "https"),
        h2_header(":authority", "localhost"), h2_header(":path", "/second")};
    SlHttp2HeaderList first_headers = {.fields = first_fields,
                                       .count = sizeof(first_fields) / sizeof(first_fields[0])};
    SlHttp2HeaderList second_headers = {.fields = second_fields,
                                        .count = sizeof(second_fields) / sizeof(second_fields[0])};
    SlBytes outbound = {0};
    SlHttp2EventList events = {0};
    size_t held_length = 0U;
    int32_t first_stream = 0;
    int32_t second_stream = 0;

    config.dispatch = dispatch_hook;
    config.dispatch_user = &hook;
    config.max_streams = 1U;
    config.max_body_bytes = 128U;
    config.max_response_body_bytes = 1024U;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_init(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_http2_session_init(&client, &client_arena,
                                  &(SlHttp2SessionConfig){.role = SL_HTTP2_SESSION_ROLE_CLIENT}),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_server_dispatcher_init(&server, &server_arena, &connection, &config),
                      SL_STATUS_OK) != 0)
    {
        return 49;
    }
    connection.scheme = sl_str_from_cstr("https");

    if (pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0 ||
        pump_client_to_server(&client, &server) != 0)
    {
        return 50;
    }
    sl_http2_session_clear_events(&client);
    sl_http2_session_clear_events(&server.session);

    if (expect_status(sl_http2_session_submit_request(&client, &first_headers, bytes_from_cstr("x"),
                                                      &first_stream),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_session_drain_output(&client, &outbound), SL_STATUS_OK) != 0)
    {
        return 51;
    }
    held_length = first_frame_size(outbound);
    if (held_length == 0U || held_length >= outbound.length ||
        receive_server_bytes(&server, sl_bytes_from_parts(outbound.ptr, held_length)) != 0)
    {
        return 52;
    }

    if (sl_http2_server_dispatcher_active_streams(&server) != 1U || hook.count != 0U) {
        return 53;
    }

    if (expect_status(sl_http2_session_submit_request(&client, &second_headers, sl_bytes_empty(),
                                                      &second_stream),
                      SL_STATUS_OK) != 0 ||
        pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 54;
    }

    events = sl_http2_session_events(&client);
    if (!saw_rst_stream(&events, second_stream, SL_HTTP2_ERROR_REFUSED_STREAM) ||
        hook.count != 0U || sl_http2_server_dispatcher_active_streams(&server) != 1U)
    {
        return 55;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_dispatch_failure_maps_to_safe_http2_response(void)
{
    unsigned char client_storage[65536];
    unsigned char server_storage[65536];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {.status_code = SL_STATUS_OUT_OF_RANGE,
                         .diag_code = SL_DIAG_HTTP_ROUTE_NOT_FOUND};
    SlHttp2HeaderField fields[] = {h2_header(":method", "GET"), h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"),
                                   h2_header(":path", "/missing")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlHttp2EventList events = {0};
    int32_t stream_id = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 10;
    }

    if (expect_status(
            sl_http2_session_submit_request(&client, &headers, sl_bytes_empty(), &stream_id),
            SL_STATUS_OK) != 0 ||
        pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 11;
    }

    events = sl_http2_session_events(&client);
    if (hook.count != 1U || !saw_response_with_body(&events, stream_id, "404", "Not Found\n") ||
        backend.active_requests != 0U)
    {
        return 12;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_trailer_pseudo_header_resets_that_stream(void)
{
    unsigned char client_storage[131072];
    unsigned char server_storage[131072];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField request_fields[] = {h2_header(":method", "POST"),
                                           h2_header(":scheme", "https"),
                                           h2_header(":authority", "localhost"),
                                           h2_header(":path", "/trailers"),
                                           h2_header("content-type", "text/plain"),
                                           h2_header("content-length", "1")};
    SlHttp2HeaderField trailer_fields[] = {h2_header(":path", "/bad-trailer")};
    SlHttp2HeaderList request_headers = {
        .fields = request_fields, .count = sizeof(request_fields) / sizeof(request_fields[0])};
    SlHttp2HeaderList trailer_headers = {
        .fields = trailer_fields, .count = sizeof(trailer_fields) / sizeof(trailer_fields[0])};
    SlBytes outbound = {0};
    SlHttp2EventList events = {0};
    size_t held_length = 0U;
    int32_t stream_id = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 56;
    }

    if (expect_status(sl_http2_session_submit_request(&client, &request_headers,
                                                      bytes_from_cstr("x"), &stream_id),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_session_drain_output(&client, &outbound), SL_STATUS_OK) != 0)
    {
        return 57;
    }
    held_length = first_frame_size(outbound);
    if (held_length == 0U || held_length >= outbound.length ||
        receive_server_bytes(&server, sl_bytes_from_parts(outbound.ptr, held_length)) != 0 ||
        send_header_block_frame(&server, stream_id,
                                (uint8_t)(SL_HTTP2_FLAG_END_HEADERS | SL_HTTP2_FLAG_END_STREAM),
                                &trailer_headers) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 58;
    }

    events = sl_http2_session_events(&client);
    if (hook.count != 0U || !saw_rst_stream(&events, stream_id, SL_HTTP2_ERROR_PROTOCOL_ERROR) ||
        sl_http2_server_dispatcher_active_streams(&server) != 0U)
    {
        return 59;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_invalid_request_headers_reset_only_that_stream(void)
{
    unsigned char client_storage[65536];
    unsigned char server_storage[65536];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField fields[] = {h2_header(":method", "GET"), h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"), h2_header(":path", "/bad"),
                                   h2_header("connection", "close")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlHttp2EventList events = {0};
    int32_t stream_id = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 20;
    }

    if (expect_status(
            sl_http2_session_submit_request(&client, &headers, sl_bytes_empty(), &stream_id),
            SL_STATUS_OK) != 0 ||
        pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 21;
    }

    events = sl_http2_session_events(&client);
    if (hook.count != 0U || !saw_rst_stream(&events, stream_id, SL_HTTP2_ERROR_PROTOCOL_ERROR) ||
        sl_http2_server_dispatcher_active_streams(&server) != 0U || backend.active_requests != 0U ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_ACCEPTED)
    {
        return 22;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_body_limit_is_enforced_per_stream(void)
{
    unsigned char client_storage[65536];
    unsigned char server_storage[65536];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2DispatchConfig config = {0};
    SlHttp2HeaderField fields[] = {h2_header(":method", "POST"),
                                   h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"),
                                   h2_header(":path", "/too-big"),
                                   h2_header("content-type", "text/plain"),
                                   h2_header("content-length", "5")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlHttp2EventList events = {0};
    int32_t stream_id = 0;

    config.dispatch = dispatch_hook;
    config.dispatch_user = &hook;
    config.max_streams = 4U;
    config.max_body_bytes = 4U;
    config.max_response_body_bytes = 1024U;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_init(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_http2_session_init(&client, &client_arena,
                                  &(SlHttp2SessionConfig){.role = SL_HTTP2_SESSION_ROLE_CLIENT}),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_server_dispatcher_init(&server, &server_arena, &connection, &config),
                      SL_STATUS_OK) != 0)
    {
        return 60;
    }
    connection.scheme = sl_str_from_cstr("https");

    if (pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0 ||
        pump_client_to_server(&client, &server) != 0)
    {
        return 61;
    }
    sl_http2_session_clear_events(&client);
    sl_http2_session_clear_events(&server.session);

    if (expect_status(sl_http2_session_submit_request(&client, &headers, bytes_from_cstr("12345"),
                                                      &stream_id),
                      SL_STATUS_OK) != 0 ||
        pump_client_to_server(&client, &server) != 0 ||
        pump_server_to_client(&server, &client) != 0)
    {
        return 62;
    }

    events = sl_http2_session_events(&client);
    if (hook.count != 0U || !saw_rst_stream(&events, stream_id, SL_HTTP2_ERROR_REFUSED_STREAM) ||
        sl_http2_server_dispatcher_active_streams(&server) != 0U)
    {
        return 63;
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_many_sequential_streams_reuse_bounded_dispatch_state(void)
{
    unsigned char client_storage[131072];
    unsigned char server_storage[32768];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField fields[] = {h2_header(":method", "GET"), h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"),
                                   h2_header(":path", "/many")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0)
    {
        return 30;
    }

    for (size_t index = 0U; index < 32U; index += 1U) {
        int32_t stream_id = 0;
        if (expect_status(
                sl_http2_session_submit_request(&client, &headers, sl_bytes_empty(), &stream_id),
                SL_STATUS_OK) != 0 ||
            pump_client_to_server(&client, &server) != 0 ||
            pump_server_to_client(&server, &client) != 0)
        {
            return 31;
        }
        sl_http2_session_clear_events(&client);
        if (hook.count != index + 1U || sl_http2_server_dispatcher_active_streams(&server) != 0U ||
            backend.active_requests != 0U)
        {
            return 32;
        }
    }

    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_coalesced_headers_rst_and_ping_does_not_send_goaway(void)
{
    unsigned char client_storage[131072];
    unsigned char server_storage[131072];
    unsigned char encode_storage[8192];
    unsigned char header_frame_storage[8192];
    unsigned char combined_storage[8192];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlArena encode_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    SlHttp2HpackEncoder encoder = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField fields[] = {h2_header(":method", "GET"), h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"),
                                   h2_header(":path", "/cancel")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlBytes header_block = {0};
    SlHttp2Frame header_frame = {0};
    SlBytes header_bytes = {0};
    SlBytes output = {0};
    static const unsigned char rst_stream[] = {0x00U, 0x00U, 0x04U, 0x03U, 0x00U, 0x00U, 0x00U,
                                               0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x08U};
    static const unsigned char ping[] = {0x00U, 0x00U, 0x08U, 0x06U, 0x00U, 0x00U,
                                         0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                                         0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    size_t combined_length = 0U;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&encode_arena, encode_storage, sizeof(encode_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0 ||
        expect_status(sl_http2_hpack_encoder_init(&encoder, 0U, sizeof(header_frame_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_hpack_encode(&encoder, &encode_arena, &headers, &header_block),
                      SL_STATUS_OK) != 0)
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        return 1;
    }

    header_frame.header.length = (uint32_t)header_block.length;
    header_frame.header.type = SL_HTTP2_FRAME_HEADERS;
    header_frame.header.flags = SL_HTTP2_FLAG_END_HEADERS;
    header_frame.header.stream_id = 1U;
    header_frame.payload = header_block;
    if (expect_status(sl_http2_frame_write(&header_frame, header_frame_storage,
                                           sizeof(header_frame_storage), &header_bytes),
                      SL_STATUS_OK) != 0 ||
        header_bytes.length + sizeof(rst_stream) + sizeof(ping) > sizeof(combined_storage))
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        return 2;
    }

    memcpy(combined_storage, header_bytes.ptr, header_bytes.length);
    combined_length += header_bytes.length;
    memcpy(combined_storage + combined_length, rst_stream, sizeof(rst_stream));
    combined_length += sizeof(rst_stream);
    memcpy(combined_storage + combined_length, ping, sizeof(ping));
    combined_length += sizeof(ping);

    if (receive_server_bytes(&server, sl_bytes_from_parts(combined_storage, combined_length)) !=
            0 ||
        expect_status(sl_http2_server_dispatcher_drain_output(&server, &output), SL_STATUS_OK) !=
            0 ||
        frame_bytes_contain_type(output, SL_HTTP2_FRAME_GOAWAY) ||
        !frame_bytes_contain_type(output, SL_HTTP2_FRAME_PING))
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        sl_http2_server_dispatcher_dispose(&server);
        sl_http2_session_dispose(&client);
        return 3;
    }

    sl_http2_hpack_encoder_dispose(&encoder);
    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

static int test_coalesced_data_after_rst_sends_stream_closed_goaway(void)
{
    unsigned char client_storage[131072];
    unsigned char server_storage[131072];
    unsigned char encode_storage[8192];
    unsigned char header_frame_storage[8192];
    unsigned char combined_storage[8192];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlArena encode_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttp2Session client = {0};
    SlHttp2ServerDispatcher server = {0};
    SlHttp2HpackEncoder encoder = {0};
    DispatchHook hook = {0};
    SlHttp2HeaderField fields[] = {h2_header(":method", "GET"), h2_header(":scheme", "https"),
                                   h2_header(":authority", "localhost"),
                                   h2_header(":path", "/cancel")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlBytes header_block = {0};
    SlHttp2Frame header_frame = {0};
    SlBytes header_bytes = {0};
    SlBytes output = {0};
    static const unsigned char rst_stream[] = {0x00U, 0x00U, 0x04U, 0x03U, 0x00U, 0x00U, 0x00U,
                                               0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x08U};
    static const unsigned char data_frame[] = {
        0x00U, 0x00U, 0x04U,        0x00U,        0x01U,        0x00U,       0x00U,
        0x00U, 0x01U, (uint8_t)'t', (uint8_t)'e', (uint8_t)'s', (uint8_t)'t'};
    size_t combined_length = 0U;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&encode_arena, encode_storage, sizeof(encode_storage)),
                      SL_STATUS_OK) != 0 ||
        init_h2_pair(&client_arena, &server_arena, &backend, &connection, &client, &server,
                     &hook) != 0 ||
        expect_status(sl_http2_hpack_encoder_init(&encoder, 0U, sizeof(header_frame_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_hpack_encode(&encoder, &encode_arena, &headers, &header_block),
                      SL_STATUS_OK) != 0)
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        return 1;
    }

    header_frame.header.length = (uint32_t)header_block.length;
    header_frame.header.type = SL_HTTP2_FRAME_HEADERS;
    header_frame.header.flags = SL_HTTP2_FLAG_END_HEADERS;
    header_frame.header.stream_id = 1U;
    header_frame.payload = header_block;
    if (expect_status(sl_http2_frame_write(&header_frame, header_frame_storage,
                                           sizeof(header_frame_storage), &header_bytes),
                      SL_STATUS_OK) != 0 ||
        header_bytes.length + sizeof(rst_stream) + sizeof(data_frame) > sizeof(combined_storage))
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        return 2;
    }

    memcpy(combined_storage, header_bytes.ptr, header_bytes.length);
    combined_length += header_bytes.length;
    memcpy(combined_storage + combined_length, rst_stream, sizeof(rst_stream));
    combined_length += sizeof(rst_stream);
    memcpy(combined_storage + combined_length, data_frame, sizeof(data_frame));
    combined_length += sizeof(data_frame);

    if (receive_server_bytes(&server, sl_bytes_from_parts(combined_storage, combined_length)) !=
            0 ||
        expect_status(sl_http2_server_dispatcher_drain_output(&server, &output), SL_STATUS_OK) !=
            0 ||
        !frame_bytes_contain_type(output, SL_HTTP2_FRAME_GOAWAY))
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        sl_http2_server_dispatcher_dispose(&server);
        sl_http2_session_dispose(&client);
        return 3;
    }

    sl_http2_hpack_encoder_dispose(&encoder);
    sl_http2_server_dispatcher_dispose(&server);
    sl_http2_session_dispose(&client);
    return 0;
}

int main(void)
{
    int result = 0;

    result = test_dispatcher_handles_multiple_streams_on_one_connection();
    if (result != 0) {
        return result;
    }
    result = test_one_bad_stream_does_not_block_a_good_stream();
    if (result != 0) {
        return result;
    }
    result = test_head_request_suppresses_body_but_keeps_content_length();
    if (result != 0) {
        return result;
    }
    result = test_active_stream_cap_resets_new_streams_until_existing_stream_completes();
    if (result != 0) {
        return result;
    }
    result = test_head_dispatch_failure_maps_405_without_body();
    if (result != 0) {
        return result;
    }
    result = test_dispatch_failure_maps_to_safe_http2_response();
    if (result != 0) {
        return result;
    }
    result = test_trailer_pseudo_header_resets_that_stream();
    if (result != 0) {
        return result;
    }
    result = test_invalid_request_headers_reset_only_that_stream();
    if (result != 0) {
        return result;
    }
    result = test_body_limit_is_enforced_per_stream();
    if (result != 0) {
        return result;
    }
    result = test_many_sequential_streams_reuse_bounded_dispatch_state();
    if (result != 0) {
        return result;
    }
    result = test_coalesced_headers_rst_and_ping_does_not_send_goaway();
    if (result != 0) {
        return result;
    }
    result = test_coalesced_data_after_rst_sends_stream_closed_goaway();
    if (result != 0) {
        return result;
    }

    return 0;
}
