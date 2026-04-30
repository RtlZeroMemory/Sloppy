#include "sloppy/http_transport.h"

#include <uv.h>

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
    return sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(str.ptr), str.length);
}

static int expect_str_equal(SlStr actual, const char* expected)
{
    return expect_true(sl_str_equal(actual, sl_str_from_cstr(expected)));
}

static int expect_bytes_equal(SlBytes actual, const char* expected)
{
    return expect_true(sl_bytes_equal(actual, bytes_from_cstr(expected)));
}

typedef struct ReadyHook
{
    size_t count;
    SlHttpMethod method;
    SlStr path;
    SlBytes body;
} ReadyHook;

static void ready_hook(SlHttpTransportConnection* connection, const SlHttpRequestLifecycle* request,
                       void* user)
{
    ReadyHook* hook = static_cast<ReadyHook*>(user);

    if (hook == nullptr || connection == nullptr || request == nullptr) {
        return;
    }
    hook->count += 1U;
    hook->method = request->head.method;
    hook->path = request->head.path;
    hook->body = request->head.body;
}

typedef struct DispatchHook
{
    size_t count;
    SlStatusCode status_code;
    SlDiagCode diag_code;
    SlHttpResponse response;
} DispatchHook;

static SlStatus dispatch_hook(SlHttpTransportConnection* connection, SlArena* arena,
                              const SlHttpRequestLifecycle* request, SlHttpResponse* out_response,
                              SlDiag* out_diag, void* user)
{
    DispatchHook* hook = static_cast<DispatchHook*>(user);

    (void)arena;
    if (hook == nullptr || connection == nullptr || request == nullptr || out_response == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    hook->count += 1U;
    if (hook->status_code != SL_STATUS_OK) {
        if (out_diag != nullptr) {
            *out_diag = {};
            out_diag->severity = SL_DIAG_SEVERITY_ERROR;
            out_diag->code = hook->diag_code;
            out_diag->message = sl_str_from_cstr("test dispatch failure");
        }
        return sl_status_from_code(hook->status_code);
    }

    *out_response = hook->response;
    return sl_status_ok();
}

typedef struct ClientConnect
{
    uv_loop_t loop;
    uv_tcp_t handle;
    uv_connect_t request;
    unsigned char read_buffer[4096];
    size_t read_length;
    int status;
    bool connected;
    bool closed;
} ClientConnect;

typedef struct ClientWrite
{
    uv_write_t request;
    bool done;
    int status;
} ClientWrite;

static void client_connect_cb(uv_connect_t* request, int status)
{
    ClientConnect* client = static_cast<ClientConnect*>(request->data);

    if (client != nullptr) {
        client->status = status;
        client->connected = status == 0;
    }
}

static void client_close_cb(uv_handle_t* handle)
{
    (void)handle;
}

static void client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* out_buffer)
{
    ClientConnect* client = static_cast<ClientConnect*>(handle->data);

    (void)suggested_size;
    if (client == nullptr || client->read_length >= sizeof(client->read_buffer)) {
        *out_buffer = uv_buf_init(nullptr, 0U);
        return;
    }
    *out_buffer =
        uv_buf_init(reinterpret_cast<char*>(client->read_buffer + client->read_length),
                    static_cast<unsigned int>(sizeof(client->read_buffer) - client->read_length));
}

static void client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buffer)
{
    ClientConnect* client = static_cast<ClientConnect*>(stream->data);

    (void)buffer;
    if (client == nullptr) {
        return;
    }
    if (nread > 0) {
        client->read_length += static_cast<size_t>(nread);
        return;
    }
    if (nread < 0) {
        client->closed = true;
        (void)uv_read_stop(stream);
    }
}

static void client_write_cb(uv_write_t* request, int status)
{
    ClientWrite* write = static_cast<ClientWrite*>(request->data);

    if (write != nullptr) {
        write->done = true;
        write->status = status;
    }
}

static int connect_client(uint32_t port, ClientConnect* client)
{
    struct sockaddr_in address;

    *client = {};
    if (uv_loop_init(&client->loop) != 0) {
        return 1;
    }
    if (uv_tcp_init(&client->loop, &client->handle) != 0) {
        (void)uv_loop_close(&client->loop);
        return 2;
    }
    client->handle.data = client;
    if (uv_ip4_addr("127.0.0.1", static_cast<int>(port), &address) != 0) {
        (void)uv_loop_close(&client->loop);
        return 3;
    }

    client->request.data = client;
    if (uv_tcp_connect(&client->request, &client->handle, (const struct sockaddr*)&address,
                       client_connect_cb) != 0)
    {
        (void)uv_loop_close(&client->loop);
        return 4;
    }
    (void)uv_run(&client->loop, UV_RUN_DEFAULT);
    return client->connected ? 0 : 5;
}

static int start_client_read(ClientConnect* client)
{
    if (client == nullptr || !client->connected) {
        return 1;
    }
    return uv_read_start(reinterpret_cast<uv_stream_t*>(&client->handle), client_alloc_cb,
                         client_read_cb) == 0
               ? 0
               : 2;
}

static int write_client_bytes(ClientConnect* client, const char* text)
{
    ClientWrite write = {};
    SlStr bytes = {};
    uv_buf_t buffer = {};

    if (client == nullptr || !client->connected || text == nullptr) {
        return 1;
    }
    bytes = sl_str_from_cstr(text);
    buffer = uv_buf_init(const_cast<char*>(bytes.ptr), static_cast<unsigned int>(bytes.length));
    write.request.data = &write;
    if (uv_write(&write.request, reinterpret_cast<uv_stream_t*>(&client->handle), &buffer, 1U,
                 client_write_cb) != 0)
    {
        return 2;
    }
    for (size_t index = 0U; index < 128U && !write.done; index += 1U) {
        (void)uv_run(&client->loop, UV_RUN_NOWAIT);
        uv_sleep(1U);
    }
    if (!write.done) {
        return 4;
    }
    return write.status == 0 ? 0 : 3;
}

static void close_client(ClientConnect* client)
{
    if (client == nullptr) {
        return;
    }
    uv_close((uv_handle_t*)&client->handle, client_close_cb);
    (void)uv_run(&client->loop, UV_RUN_DEFAULT);
    (void)uv_loop_close(&client->loop);
}

static SlHttpTransportConfig small_config(ReadyHook* hook)
{
    SlHttpTransportConfig config = {};

    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = 0U;
    config.max_connections = 1U;
    config.max_active_requests = 1U;
    config.connection_capacity = 1U;
    config.backlog = 8;
    config.max_request_head_bytes = 256U;
    config.request_arena_bytes = 4096U;
    config.read_chunk_bytes = 64U;
    config.max_response_bytes = 4096U;
    config.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    config.parse.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    config.parse.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    config.parse.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    config.parse.max_total_header_bytes = 256U;
    config.parse.max_body_length = 32U;
    config.on_request_ready = hook == nullptr ? nullptr : ready_hook;
    config.on_request_ready_user = hook;
    return config;
}

static int poll_until_write_completed(SlHttpTransportServer* server)
{
    SlDiag diag = {};

    for (size_t index = 0U; index < 64U; index += 1U) {
        if (server->connections[0].write_completed) {
            return 0;
        }
        if (expect_status(sl_http_transport_server_poll(server, &diag), SL_STATUS_OK) != 0) {
            return 1;
        }
    }
    return server->connections[0].write_completed ? 0 : 2;
}

static int poll_server_and_client_until_closed(SlHttpTransportServer* server, ClientConnect* client)
{
    SlDiag diag = {};

    for (size_t index = 0U; index < 128U; index += 1U) {
        if (expect_status(sl_http_transport_server_poll(server, &diag), SL_STATUS_OK) != 0) {
            return 1;
        }
        (void)uv_run(&client->loop, UV_RUN_NOWAIT);
        if (server->connections[0].write_completed && client->closed) {
            return 0;
        }
    }
    return 2;
}

static int poll_server_and_client_until_response(SlHttpTransportServer* server,
                                                 ClientConnect* client)
{
    SlDiag diag = {};

    for (size_t index = 0U; index < 512U; index += 1U) {
        if (expect_status(sl_http_transport_server_poll(server, &diag), SL_STATUS_OK) != 0) {
            return 1;
        }
        (void)uv_run(&client->loop, UV_RUN_NOWAIT);
        if (server->connections[0].write_completed || client->closed) {
            return 0;
        }
        uv_sleep(1U);
    }
    return 2;
}

static int start_one_connection(SlArena* arena, SlHttpTransportServer* server,
                                ClientConnect* client, ReadyHook* hook)
{
    SlHttpTransportConfig config = small_config(hook);
    SlDiag diag = {};

    if (expect_status(sl_http_transport_server_init(server, arena, &config, &diag), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_transport_server_listen(server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(server), client) != 0 ||
        expect_status(sl_http_transport_server_poll(server, &diag), SL_STATUS_OK) != 0 ||
        server->connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD)
    {
        return 1;
    }

    return 0;
}

static void stop_one_connection(SlHttpTransportServer* server, ClientConnect* client)
{
    SlDiag diag = {};

    if (server != nullptr) {
        (void)sl_http_transport_connection_close(&server->connections[0], &diag);
        (void)sl_http_transport_server_poll(server, &diag);
        (void)sl_http_transport_server_stop(server, &diag);
        (void)sl_http_transport_server_dispose(server, &diag);
    }
    close_client(client);
}

static int test_config_validation_and_lifecycle(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    SlDiag diag = {};

    config = small_config(nullptr);

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_CREATED ||
        server.config.max_connections != 1U)
    {
        return 1;
    }
    if (expect_status(sl_http_transport_server_stop(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED ||
        expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED)
    {
        return 2;
    }

    sl_arena_reset(&arena);
    server = {};
    config = small_config(nullptr);
    config.parse.max_headers = 0U;
    if (expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 3;
    }
    if (server.config.parse.max_headers != SL_HTTP_DEFAULT_MAX_HEADERS) {
        return 4;
    }
    if (expect_status(sl_http_transport_server_stop(&server, &diag), SL_STATUS_OK) != 0) {
        return 5;
    }
    if (expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK) != 0) {
        return 6;
    }

    sl_arena_reset(&arena);
    config = small_config(nullptr);
    config.host = sl_str_from_cstr("not an address");
    server = {};
    if (expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_HTTP_TRANSPORT_CONFIG)
    {
        return 7;
    }

    sl_arena_reset(&arena);
    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = 70000U;
    server = {};
    if (expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_HTTP_TRANSPORT_CONFIG)
    {
        return 8;
    }

    return 0;
}

static int test_listen_accept_capacity_and_cleanup(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    SlDiag diag = {};
    ClientConnect first = {};
    ClientConnect second = {};
    uint32_t port = 0U;

    config = small_config(nullptr);

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING)
    {
        return 10;
    }

    if (expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_INVALID_STATE) !=
            0 ||
        diag.code != SL_DIAG_APP_LIFECYCLE)
    {
        return 11;
    }

    port = sl_http_transport_server_bound_port(&server);
    if (port == 0U || connect_client(port, &first) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 1U ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD ||
        server.backend.active_connections != 1U)
    {
        close_client(&first);
        return 12;
    }

    if (connect_client(port, &second) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 1U ||
        server.rejected_connections == 0U)
    {
        close_client(&first);
        close_client(&second);
        return 13;
    }

    if (expect_status(sl_http_transport_connection_close(&server.connections[0], &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 0U)
    {
        close_client(&first);
        close_client(&second);
        return 14;
    }

    close_client(&first);
    close_client(&second);

    if (expect_status(sl_http_transport_server_stop(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED ||
        expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED)
    {
        return 15;
    }

    return 0;
}

static int test_dispose_after_listen_closes_active_connection(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    SlDiag diag = {};
    ClientConnect client = {};

    config = small_config(nullptr);
    config.max_connections = 2U;
    config.max_active_requests = 2U;
    config.connection_capacity = 2U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 1U)
    {
        close_client(&client);
        return 20;
    }

    if (expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED ||
        sl_http_transport_server_active_connections(&server) != 0U)
    {
        close_client(&client);
        return 21;
    }

    close_client(&client);
    return 0;
}

static int test_ready_get_and_split_head(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    ReadyHook hook = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, &hook) != 0)
    {
        return 30;
    }
    if (expect_status(sl_http_transport_connection_feed_test(&server.connections[0],
                                                             bytes_from_cstr("GET /sp"), &diag),
                      SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD)
    {
        stop_one_connection(&server, &client);
        return 31;
    }
    if (expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0], bytes_from_cstr("lit HTTP/1.1\r\n\r\n"), &diag),
                      SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_REQUEST_READY ||
        hook.count != 1U || hook.method != SL_HTTP_METHOD_GET ||
        expect_str_equal(hook.path, "/split") != 0 || server.backend.active_requests != 1U)
    {
        stop_one_connection(&server, &client);
        return 32;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_ready_request_without_hook_closes_and_releases(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, nullptr) != 0)
    {
        return 40;
    }
    if (expect_status(
            sl_http_transport_connection_feed_test(
                &server.connections[0], bytes_from_cstr("GET /done HTTP/1.1\r\n\r\n"), &diag),
            SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U)
    {
        stop_one_connection(&server, &client);
        return 41;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_body_same_chunk_split_chunk_and_empty_body(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    ReadyHook hook = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, &hook) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST /body HTTP/1.1\r\nContent-Type: text/plain\r\n"
                                          "Content-Length: 5\r\n\r\nhello"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        hook.count != 1U || expect_str_equal(hook.path, "/body") != 0 ||
        expect_bytes_equal(hook.body, "hello") != 0)
    {
        stop_one_connection(&server, &client);
        return 40;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    hook = {};
    if (start_one_connection(&arena, &server, &client, &hook) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("PUT /split-body HTTP/1.1\r\nContent-Type: text/plain\r\n"
                                          "Content-Length: 5\r\n\r\nhe"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY ||
        expect_status(sl_http_transport_connection_feed_test(&server.connections[0],
                                                             bytes_from_cstr("llo"), &diag),
                      SL_STATUS_OK) != 0 ||
        hook.count != 1U || expect_str_equal(hook.path, "/split-body") != 0 ||
        expect_bytes_equal(hook.body, "hello") != 0)
    {
        stop_one_connection(&server, &client);
        return 41;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    hook = {};
    if (start_one_connection(&arena, &server, &client, &hook) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("PATCH /empty HTTP/1.1\r\nContent-Type: text/plain\r\n"
                                          "Content-Length: 0\r\n\r\n"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        hook.count != 1U || expect_str_equal(hook.path, "/empty") != 0 || hook.body.length != 0U)
    {
        stop_one_connection(&server, &client);
        return 42;
    }
    stop_one_connection(&server, &client);

    return 0;
}

static int test_limits_and_malformed_requests_are_deterministic(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                          "aaaaaaaaaaaaaaaaaaaa"),
                          &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_BYTES_LIMIT ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR)
    {
        stop_one_connection(&server, &client);
        return 50;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    diag = {};
    if (start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(
            sl_http_transport_connection_feed_test(
                &server.connections[0], bytes_from_cstr("GET / HTTP/1.1\r\nBad\r\n\r\n"), &diag),
            SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        stop_one_connection(&server, &client);
        return 51;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    diag = {};
    config = small_config(nullptr);
    config.parse.max_headers = 1U;
    if (expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET / HTTP/1.1\r\nA: 1\r\nB: 2\r\n\r\n"), &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_LIMIT)
    {
        stop_one_connection(&server, &client);
        return 52;
    }
    stop_one_connection(&server, &client);

    return 0;
}

static int test_body_policy_and_pipelining_rejections(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"),
                          &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_HTTP_UNSUPPORTED_BODY)
    {
        stop_one_connection(&server, &client);
        return 60;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    diag = {};
    if (start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST / HTTP/1.1\r\nContent-Type: text/plain\r\n"
                                          "Content-Length: 33\r\n\r\n"),
                          &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_BODY_LIMIT)
    {
        stop_one_connection(&server, &client);
        return 61;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    diag = {};
    if (start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(
            sl_http_transport_connection_feed_test(
                &server.connections[0],
                bytes_from_cstr("POST / HTTP/1.1\r\nContent-Type: application/octet-stream\r\n"
                                "Content-Length: 2\r\n\r\nhi"),
                &diag),
            SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE)
    {
        stop_one_connection(&server, &client);
        return 62;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    diag = {};
    if (start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /one HTTP/1.1\r\n\r\nGET /two HTTP/1.1\r\n\r\n"),
                          &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED)
    {
        stop_one_connection(&server, &client);
        return 63;
    }
    stop_one_connection(&server, &client);

    return 0;
}

static int test_disconnect_cleanup_paths(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, nullptr) != 0)
    {
        return 70;
    }
    close_client(&client);
    if (expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 0U)
    {
        stop_one_connection(&server, &client);
        return 71;
    }
    (void)sl_http_transport_server_stop(&server, &diag);
    (void)sl_http_transport_server_dispose(&server, &diag);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    diag = {};
    if (start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST /body HTTP/1.1\r\nContent-Type: text/plain\r\n"
                                          "Content-Length: 5\r\n\r\nhe"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY ||
        expect_status(sl_http_transport_connection_close(&server.connections[0], &diag),
                      SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U)
    {
        stop_one_connection(&server, &client);
        return 72;
    }
    stop_one_connection(&server, &client);

    return 0;
}

static int test_dispatch_success_writes_response_and_closes(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    DispatchHook dispatch = {};
    SlDiag diag = {};

    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("hello\n"));
    config = small_config(nullptr);
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /ok HTTP/1.1\r\nHost: local\r\n\r\n"), &diag),
                      SL_STATUS_OK) != 0 ||
        dispatch.count != 1U || server.connections[0].response_length == 0U ||
        expect_bytes_equal(sl_bytes_from_parts(server.connections[0].response_storage,
                                               server.connections[0].response_length),
                           "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; "
                           "charset=utf-8\r\nContent-Length: 6\r\n\r\nhello\n") != 0 ||
        poll_server_and_client_until_closed(&server, &client) != 0 ||
        expect_bytes_equal(sl_bytes_from_parts(client.read_buffer, client.read_length),
                           "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; "
                           "charset=utf-8\r\nContent-Length: 6\r\n\r\nhello\n") != 0 ||
        sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U)
    {
        stop_one_connection(&server, &client);
        return 80;
    }

    if (expect_status(
            sl_http_transport_connection_feed_test(
                &server.connections[0], bytes_from_cstr("GET /again HTTP/1.1\r\n\r\n"), &diag),
            SL_STATUS_INVALID_STATE) != 0 ||
        dispatch.count != 1U)
    {
        stop_one_connection(&server, &client);
        return 81;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_dispatch_failures_map_to_safe_responses(void)
{
    static const struct
    {
        SlStatusCode status_code;
        SlDiagCode diag_code;
        const char* expected;
    } cases[] = {
        {SL_STATUS_OUT_OF_RANGE, SL_DIAG_HTTP_ROUTE_NOT_FOUND,
         "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Type: text/plain; "
         "charset=utf-8\r\nContent-Length: 10\r\n\r\nNot Found\n"},
        {SL_STATUS_UNSUPPORTED, SL_DIAG_HTTP_UNSUPPORTED_METHOD,
         "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\nContent-Type: text/plain; "
         "charset=utf-8\r\nContent-Length: 19\r\n\r\nMethod Not Allowed\n"},
        {SL_STATUS_INVALID_STATE, SL_DIAG_ENGINE_EXCEPTION,
         "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Type: text/plain; "
         "charset=utf-8\r\nContent-Length: 22\r\n\r\nSloppy handler failed\n"},
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[65536];
        SlArena arena = {};
        SlHttpTransportServer server = {};
        SlHttpTransportConfig config = {};
        ClientConnect client = {};
        DispatchHook dispatch = {};
        SlDiag diag = {};

        dispatch.status_code = cases[index].status_code;
        dispatch.diag_code = cases[index].diag_code;
        config = small_config(nullptr);
        config.dispatch = dispatch_hook;
        config.dispatch_user = &dispatch;

        if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
            expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                          SL_STATUS_OK) != 0 ||
            expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
            connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
            expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
            expect_status(
                sl_http_transport_connection_feed_test(
                    &server.connections[0], bytes_from_cstr("GET /mapped HTTP/1.1\r\n\r\n"), &diag),
                SL_STATUS_OK) != 0 ||
            dispatch.count != 1U ||
            expect_bytes_equal(sl_bytes_from_parts(server.connections[0].response_storage,
                                                   server.connections[0].response_length),
                               cases[index].expected) != 0 ||
            poll_until_write_completed(&server) != 0 ||
            sl_http_transport_server_active_connections(&server) != 0U ||
            server.backend.active_requests != 0U)
        {
            stop_one_connection(&server, &client);
            return 90 + (int)index;
        }

        stop_one_connection(&server, &client);
    }

    return 0;
}

static int test_response_buffer_capacity_failure_is_deterministic(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    DispatchHook dispatch = {};
    SlDiag diag = {};

    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("this body cannot fit\n"));
    config = small_config(nullptr);
    config.max_response_bytes = 16U;
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_http_transport_connection_feed_test(
                &server.connections[0], bytes_from_cstr("GET /big HTTP/1.1\r\n\r\n"), &diag),
            SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED || dispatch.count != 1U ||
        server.backend.active_requests != 0U)
    {
        stop_one_connection(&server, &client);
        return 100;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_shutdown_rejects_new_accepts_and_closes_head_read(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    ClientConnect first = {};
    ClientConnect second = {};
    SlDiag diag = {};
    uint32_t port = 0U;

    config = small_config(nullptr);
    config.max_connections = 2U;
    config.max_active_requests = 2U;
    config.connection_capacity = 2U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0)
    {
        return 110;
    }
    port = sl_http_transport_server_bound_port(&server);
    if (connect_client(port, &first) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD ||
        expect_status(sl_http_transport_server_stop(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED ||
        sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U)
    {
        close_client(&first);
        return 111;
    }
    if (connect_client(port, &second) == 0) {
        close_client(&second);
        close_client(&first);
        return 112;
    }

    close_client(&first);
    return expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK);
}

static int test_shutdown_during_body_read_cancels_cleanup_once(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, nullptr) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST /body HTTP/1.1\r\nContent-Type: text/plain\r\n"
                                          "Content-Length: 5\r\n\r\nhe"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY ||
        expect_status(sl_http_transport_server_stop(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U ||
        expect_status(sl_http_transport_connection_close(&server.connections[0], &diag),
                      SL_STATUS_OK) != 0 ||
        sl_http_transport_server_active_connections(&server) != 0U)
    {
        close_client(&client);
        return 120;
    }

    close_client(&client);
    return expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK);
}

static int test_header_and_body_timeout_write_408_or_close(void)
{
    static const char expected[] =
        "HTTP/1.1 408 Request Timeout\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 16\r\n\r\nRequest Timeout\n";

    for (size_t index = 0U; index < 3U; index += 1U) {
        unsigned char storage[65536];
        SlArena arena = {};
        SlHttpTransportConfig config = {};
        SlHttpTransportServer server = {};
        ClientConnect client = {};
        SlDiag diag = {};

        config = small_config(nullptr);
        config.header_read_timeout_ms = index == 0U ? 1U : 1000U;
        config.body_read_timeout_ms = index == 1U ? 1U : 1000U;
        config.request_timeout_ms = index == 2U ? 1U : 1000U;

        if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
            expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                          SL_STATUS_OK) != 0 ||
            expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
            connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
            start_client_read(&client) != 0 ||
            expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0)
        {
            close_client(&client);
            return 130 + (int)index;
        }
        if (index == 1U || index == 2U) {
            if (write_client_bytes(&client, "POST /slow HTTP/1.1\r\nContent-Type: text/plain\r\n"
                                            "Content-Length: 5\r\n\r\nhe") != 0)
            {
                stop_one_connection(&server, &client);
                return 132;
            }
        }
        if (poll_server_and_client_until_response(&server, &client) != 0 ||
            server.connections[0].last_diag.code != SL_DIAG_HTTP_REQUEST_TIMEOUT ||
            expect_bytes_equal(sl_bytes_from_parts(client.read_buffer, client.read_length),
                               expected) != 0 ||
            sl_http_transport_server_active_connections(&server) != 0U ||
            server.backend.active_requests != 0U)
        {
            stop_one_connection(&server, &client);
            return 133 + (int)index;
        }
        stop_one_connection(&server, &client);
    }

    return 0;
}

static int test_shutdown_during_response_write_is_cleanup_only(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    DispatchHook dispatch = {};
    SlDiag diag = {};

    dispatch.status_code = SL_STATUS_OK;
    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("ok\n"));
    config = small_config(nullptr);
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;
    config.write_timeout_ms = 1000U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_http_transport_connection_feed_test(
                &server.connections[0], bytes_from_cstr("GET /write HTTP/1.1\r\n\r\n"), &diag),
            SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE ||
        !server.connections[0].write_started || server.connections[0].write_completed ||
        server.backend.active_requests != 1U)
    {
        stop_one_connection(&server, &client);
        return 140;
    }

    if (expect_status(sl_http_transport_server_stop(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_state(&server) != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED ||
        sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U ||
        server.connections[0].request.state != SL_HTTP_REQUEST_STATE_CLOSED ||
        !server.connections[0].write_completed)
    {
        close_client(&client);
        return 141;
    }

    close_client(&client);
    return expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK);
}

int main(void)
{
    int result = test_config_validation_and_lifecycle();

    if (result != 0) {
        return result;
    }
    result = test_listen_accept_capacity_and_cleanup();
    if (result != 0) {
        return result;
    }
    result = test_dispose_after_listen_closes_active_connection();
    if (result != 0) {
        return result;
    }
    result = test_ready_get_and_split_head();
    if (result != 0) {
        return result;
    }
    result = test_ready_request_without_hook_closes_and_releases();
    if (result != 0) {
        return result;
    }
    result = test_body_same_chunk_split_chunk_and_empty_body();
    if (result != 0) {
        return result;
    }
    result = test_limits_and_malformed_requests_are_deterministic();
    if (result != 0) {
        return result;
    }
    result = test_body_policy_and_pipelining_rejections();
    if (result != 0) {
        return result;
    }
    result = test_disconnect_cleanup_paths();
    if (result != 0) {
        return result;
    }
    result = test_dispatch_success_writes_response_and_closes();
    if (result != 0) {
        return result;
    }
    result = test_dispatch_failures_map_to_safe_responses();
    if (result != 0) {
        return result;
    }
    result = test_response_buffer_capacity_failure_is_deterministic();
    if (result != 0) {
        return result;
    }
    result = test_shutdown_rejects_new_accepts_and_closes_head_read();
    if (result != 0) {
        return result;
    }
    result = test_shutdown_during_body_read_cancels_cleanup_once();
    if (result != 0) {
        return result;
    }
    result = test_header_and_body_timeout_write_408_or_close();
    if (result != 0) {
        return result;
    }
    return test_shutdown_during_response_write_is_cleanup_only();
}
