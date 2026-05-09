#include "sloppy/http2_dispatch.h"

#include <stdbool.h>

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

int main(void)
{
    int result = 0;

    result = test_dispatcher_handles_multiple_streams_on_one_connection();
    if (result != 0) {
        return result;
    }
    result = test_dispatch_failure_maps_to_safe_http2_response();
    if (result != 0) {
        return result;
    }
    result = test_invalid_request_headers_reset_only_that_stream();
    if (result != 0) {
        return result;
    }
    result = test_many_sequential_streams_reuse_bounded_dispatch_state();
    if (result != 0) {
        return result;
    }

    return 0;
}
