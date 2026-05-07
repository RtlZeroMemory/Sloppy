#include "sloppy/http_transport.h"

#include <cstring>

#include <uv.h>

#include <cstring>

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
    SlHttpMethod method;
    SlStr path;
    SlBytes body;
} DispatchHook;

typedef struct SequenceDispatchHook
{
    size_t count;
    const char* bodies[16];
    SlHttpResponseStreamChunk stream_chunks[4];
    SlHttpResponse stream_response;
    bool use_stream_response;
    bool mismatch;
    const char* expected_paths[16];
    const char* expected_request_bodies[16];
    SlHttpMethod methods[16];
    SlStr paths[16];
    SlBytes request_bodies[16];
} SequenceDispatchHook;

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
    hook->method = request->head.method;
    hook->path = request->head.path;
    hook->body = request->head.body;
    if (hook->status_code != SL_STATUS_OK) {
        if (out_diag != nullptr) {
            *out_diag = {};
            out_diag->severity = SL_DIAG_SEVERITY_ERROR;
            out_diag->code = hook->diag_code;
            out_diag->message = sl_str_from_cstr("test dispatch failure");
        }
        return sl_status_from_code(hook->status_code);
    }

    if (hook->response.kind == SL_HTTP_RESPONSE_STREAM && hook->response.stream_chunk_count != 0U) {
        void* chunk_storage = nullptr;
        SlHttpResponse response = hook->response;
        SlHttpResponseStreamChunk* chunks = nullptr;
        size_t chunk_bytes = sizeof(SlHttpResponseStreamChunk) * response.stream_chunk_count;
        SlStatus status =
            sl_arena_alloc(arena, chunk_bytes, alignof(SlHttpResponseStreamChunk), &chunk_storage);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        chunks = static_cast<SlHttpResponseStreamChunk*>(chunk_storage);
        for (size_t index = 0U; index < response.stream_chunk_count; index += 1U) {
            SlOwnedBytes copy = {};
            status = sl_bytes_copy_to_arena(arena, response.stream_chunks[index].bytes, &copy);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            chunks[index].bytes = sl_owned_bytes_as_view(copy);
        }
        response.stream_chunks = chunks;
        *out_response = response;
        return sl_status_ok();
    }

    *out_response = hook->response;
    return sl_status_ok();
}

static SlStatus sequence_dispatch_hook(SlHttpTransportConnection* connection, SlArena* arena,
                                       const SlHttpRequestLifecycle* request,
                                       SlHttpResponse* out_response, SlDiag* out_diag, void* user)
{
    SequenceDispatchHook* hook = static_cast<SequenceDispatchHook*>(user);
    size_t index = 0U;

    (void)connection;
    (void)arena;
    (void)out_diag;
    if (hook == nullptr || request == nullptr || out_response == nullptr ||
        hook->count >= sizeof(hook->bodies) / sizeof(hook->bodies[0]))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    index = hook->count;
    hook->methods[index] = request->head.method;
    hook->paths[index] = request->head.path;
    hook->request_bodies[index] = request->head.body;
    if (hook->expected_paths[index] != nullptr &&
        !sl_str_equal(request->head.path, sl_str_from_cstr(hook->expected_paths[index])))
    {
        hook->mismatch = true;
    }
    if (hook->expected_request_bodies[index] != nullptr &&
        !sl_bytes_equal(request->head.body, bytes_from_cstr(hook->expected_request_bodies[index])))
    {
        hook->mismatch = true;
    }
    if (hook->expected_request_bodies[index] == nullptr && request->head.body.length != 0U) {
        hook->mismatch = true;
    }
    hook->count += 1U;

    if (hook->use_stream_response) {
        SlHttpResponse response = hook->stream_response;
        SlHttpResponseStreamChunk* chunks = nullptr;
        size_t chunk_bytes = sizeof(SlHttpResponseStreamChunk) * response.stream_chunk_count;
        void* chunk_storage = nullptr;
        SlStatus status = sl_status_ok();

        if (response.stream_chunk_count != 0U) {
            status = sl_arena_alloc(arena, chunk_bytes, alignof(SlHttpResponseStreamChunk),
                                    &chunk_storage);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            chunks = static_cast<SlHttpResponseStreamChunk*>(chunk_storage);
            for (size_t chunk_index = 0U; chunk_index < response.stream_chunk_count;
                 chunk_index += 1U)
            {
                SlOwnedBytes copy = {};
                status =
                    sl_bytes_copy_to_arena(arena, response.stream_chunks[chunk_index].bytes, &copy);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
                chunks[chunk_index].bytes = sl_owned_bytes_as_view(copy);
            }
            response.stream_chunks = chunks;
        }
        *out_response = response;
        return sl_status_ok();
    }

    *out_response = sl_http_response_text(
        200U, sl_str_from_cstr(hook->bodies[index] == nullptr ? "ok\n" : hook->bodies[index]));
    return sl_status_ok();
}

static SlStatus stack_stream_dispatch_hook(SlHttpTransportConnection* connection, SlArena* arena,
                                           const SlHttpRequestLifecycle* request,
                                           SlHttpResponse* out_response, SlDiag* out_diag,
                                           void* user)
{
    unsigned char chunk_storage[5] = {'s', 't', 'a', 'c', 'k'};
    SlHttpResponseStreamChunk chunks[1] = {};
    DispatchHook* hook = static_cast<DispatchHook*>(user);

    (void)arena;
    (void)out_diag;
    if (hook == nullptr || connection == nullptr || request == nullptr || out_response == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    hook->count += 1U;
    chunks[0].bytes = sl_bytes_from_parts(chunk_storage, sizeof(chunk_storage));
    *out_response = sl_http_response_stream(200U, sl_str_from_cstr("text/plain"), chunks, 1U);
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
    bool loop_initialized;
    bool handle_initialized;
    bool connected;
    bool closed;
} ClientConnect;

typedef struct ClientWrite
{
    uv_write_t request;
    bool done;
    int status;
} ClientWrite;

static void close_client(ClientConnect* client);

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
    int last_result = 0;

    for (size_t attempt = 0U; attempt < 16U; attempt += 1U) {
        struct sockaddr_in address;

        *client = {};
        if (uv_loop_init(&client->loop) != 0) {
            return 1;
        }
        client->loop_initialized = true;
        if (uv_tcp_init(&client->loop, &client->handle) != 0) {
            (void)uv_loop_close(&client->loop);
            client->loop_initialized = false;
            return 2;
        }
        client->handle_initialized = true;
        client->handle.data = client;
        if (uv_ip4_addr("127.0.0.1", static_cast<int>(port), &address) != 0) {
            close_client(client);
            return 3;
        }

        client->request.data = client;
        if (uv_tcp_connect(&client->request, &client->handle, (const struct sockaddr*)&address,
                           client_connect_cb) != 0)
        {
            close_client(client);
            return 4;
        }
        (void)uv_run(&client->loop, UV_RUN_DEFAULT);
        if (client->connected) {
            return 0;
        }
        last_result = 5;
        close_client(client);
        uv_sleep(5U);
    }
    return last_result;
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
    if (client == nullptr || !client->loop_initialized) {
        return;
    }

    if (client->handle_initialized && !uv_is_closing((uv_handle_t*)&client->handle)) {
        uv_close((uv_handle_t*)&client->handle, client_close_cb);
    }
    (void)uv_run(&client->loop, UV_RUN_DEFAULT);
    (void)uv_loop_close(&client->loop);
    client->handle_initialized = false;
    client->loop_initialized = false;
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
    config.keep_alive_disabled = true;
    config.on_request_ready = hook == nullptr ? nullptr : ready_hook;
    config.on_request_ready_user = hook;
    return config;
}

static SlHttpTransportConfig keep_alive_config(ReadyHook* hook)
{
    SlHttpTransportConfig config = small_config(hook);

    config.keep_alive_disabled = false;
    config.keep_alive_idle_timeout_ms = 1000U;
    config.max_requests_per_connection = 4U;
    return config;
}

static void stop_one_connection(SlHttpTransportServer* server, ClientConnect* client);
static int poll_server_and_client_until_closed(SlHttpTransportServer* server,
                                               ClientConnect* client);
static int poll_until_connection_state(SlHttpTransportServer* server, ClientConnect* client,
                                       SlHttpTransportConnectionState state, size_t min_read);

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

    for (size_t index = 0U; index < 512U; index += 1U) {
        if (expect_status(sl_http_transport_server_poll(server, &diag), SL_STATUS_OK) != 0) {
            return 1;
        }
        (void)uv_run(&client->loop, UV_RUN_NOWAIT);
        if (server->connections[0].write_completed && client->closed) {
            return 0;
        }
        uv_sleep(1U);
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

static int run_localhost_request(const SlHttpTransportConfig* config, const char* request,
                                 SlBytes* out_response, SlHttpTransportServer* out_server,
                                 DispatchHook* dispatch)
{
    static unsigned char storage[65536];
    static unsigned char response_storage[4096];
    static SlHttpTransportConnection connection_snapshot;
    SlArena arena = {};
    SlByteBuilder response_builder = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    SlDiag diag = {};
    int result = 0;

    if (out_response != nullptr) {
        *out_response = {};
    }
    if (out_server != nullptr) {
        *out_server = {};
    }
    if (config == nullptr || request == nullptr) {
        return 1;
    }

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        sl_http_transport_server_bound_port(&server) == 0U ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        write_client_bytes(&client, request) != 0 ||
        poll_server_and_client_until_closed(&server, &client) != 0)
    {
        result = 2;
        goto cleanup;
    }

    if (out_response != nullptr) {
        if (expect_status(sl_byte_builder_init_fixed(&response_builder, response_storage,
                                                     sizeof(response_storage)),
                          SL_STATUS_OK) != 0 ||
            expect_status(
                sl_byte_builder_append_bytes(
                    &response_builder, sl_bytes_from_parts(client.read_buffer, client.read_length)),
                SL_STATUS_OK) != 0)
        {
            result = 3;
            goto cleanup;
        }
        *out_response = sl_byte_builder_view(&response_builder);
    }
    if (sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U || !client.closed ||
        (dispatch != nullptr && dispatch->status_code == SL_STATUS_OK && dispatch->count != 1U))
    {
        result = 4;
        goto cleanup;
    }
    if (out_server != nullptr) {
        connection_snapshot = server.connections[0];
        *out_server = server;
        out_server->connections = &connection_snapshot;
    }

cleanup:
    stop_one_connection(&server, &client);
    return result;
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

    if (server != nullptr &&
        sl_http_transport_server_state(server) != SL_HTTP_TRANSPORT_SERVER_STATE_NONE)
    {
        if (server->connections != nullptr && server->connection_capacity > 0U) {
            (void)sl_http_transport_connection_close(&server->connections[0], &diag);
            (void)sl_http_transport_server_poll(server, &diag);
        }
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
    if (server.config.parse.max_headers != 0U) {
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
                          &server.connections[0],
                          bytes_from_cstr("lit HTTP/1.1\r\nHost: example.test\r\n\r\n"), &diag),
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
    if (expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /done HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                          &diag),
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
                          bytes_from_cstr("POST /body HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: text/plain\r\n"
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
                          bytes_from_cstr("PUT /split-body HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: text/plain\r\n"
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
                          bytes_from_cstr("PATCH /empty HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: text/plain\r\n"
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

static int test_chunked_request_decoding_success_cases(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    ReadyHook hook = {};
    SlDiag diag = {};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        start_one_connection(&arena, &server, &client, &hook) != 0 ||
        expect_status(
            sl_http_transport_connection_feed_test(
                &server.connections[0],
                bytes_from_cstr("POST /chunk HTTP/1.1\r\nHost: example.test\r\n"
                                "Content-Type: text/plain\r\n"
                                "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n"),
                &diag),
            SL_STATUS_OK) != 0 ||
        hook.count != 1U || expect_str_equal(hook.path, "/chunk") != 0 ||
        expect_bytes_equal(hook.body, "hello") != 0)
    {
        stop_one_connection(&server, &client);
        return 430;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    hook = {};
    diag = {};
    if (start_one_connection(&arena, &server, &client, &hook) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST /multi HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: application/json\r\n"
                                          "Transfer-Encoding: chunked\r\n\r\nA\r\nhello"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0], bytes_from_cstr("world\r\n0\r\n\r\n"), &diag),
                      SL_STATUS_OK) != 0 ||
        hook.count != 1U || expect_bytes_equal(hook.body, "helloworld") != 0)
    {
        stop_one_connection(&server, &client);
        return 431;
    }
    stop_one_connection(&server, &client);

    sl_arena_reset(&arena);
    server = {};
    client = {};
    hook = {};
    diag = {};
    if (start_one_connection(&arena, &server, &client, &hook) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST /empty HTTP/1.1\r\nHost: example.test\r\n"
                                          "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        hook.count != 1U || hook.body.length != 0U)
    {
        stop_one_connection(&server, &client);
        return 432;
    }
    stop_one_connection(&server, &client);

    return 0;
}

static int test_chunked_request_decoding_allows_bounded_wire_overhead(void)
{
    static const char request[] = "POST /tiny HTTP/1.1\r\nHost: example.test\r\n"
                                  "Transfer-Encoding: chunked\r\n\r\n"
                                  "1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n"
                                  "1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n"
                                  "1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n"
                                  "1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n1\r\na\r\n"
                                  "0\r\n\r\n";
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    ReadyHook hook = {};
    SlDiag diag = {};

    config = small_config(&hook);
    config.max_request_head_bytes = 96U;
    config.parse.max_body_length = 20U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(&server.connections[0],
                                                             bytes_from_cstr(request), &diag),
                      SL_STATUS_OK) != 0 ||
        hook.count != 1U || expect_bytes_equal(hook.body, "aaaaaaaaaaaaaaaaaaaa") != 0)
    {
        stop_one_connection(&server, &client);
        return 433;
    }
    stop_one_connection(&server, &client);
    return 0;
}

static int test_chunked_request_decoding_rejections(void)
{
    static const struct
    {
        const char* request;
        SlStatusCode status_code;
        SlDiagCode diag_code;
        bool missing_final_chunk_on_close;
    } cases[] = {
        {"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n\r\n"
         "z\r\nx\r\n0\r\n\r\n",
         SL_STATUS_INVALID_ARGUMENT, SL_DIAG_HTTP_CHUNK_SIZE_INVALID, false},
        {"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n\r\n"
         "2\r\nx\n0\r\n\r\n",
         SL_STATUS_INVALID_ARGUMENT, SL_DIAG_HTTP_CHUNK_DELIMITER_INVALID, false},
        {"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n\r\n21\r\n"
         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n0\r\n\r\n",
         SL_STATUS_CAPACITY_EXCEEDED, SL_DIAG_HTTP_BODY_LIMIT, false},
        {"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n\r\n20\r\n",
         SL_STATUS_OK, SL_DIAG_NONE, true},
        {"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n"
         "Content-Length: 1\r\n\r\n0\r\n\r\n",
         SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_HTTP_REQUEST, false},
        {"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: gzip\r\n\r\n",
         SL_STATUS_UNSUPPORTED, SL_DIAG_HTTP_UNSUPPORTED_BODY, false},
        {"POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n\r\n"
         "0\r\nX: y\r\n\r\n",
         SL_STATUS_UNSUPPORTED, SL_DIAG_HTTP_TRAILERS_UNSUPPORTED, false},
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[65536];
        SlArena arena = {};
        SlHttpTransportServer server = {};
        ClientConnect client = {};
        SlDiag diag = {};

        if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
            start_one_connection(&arena, &server, &client, nullptr) != 0)
        {
            return 440 + (int)index;
        }
        if (cases[index].missing_final_chunk_on_close) {
            if (expect_status(
                    sl_http_transport_connection_feed_test(
                        &server.connections[0], bytes_from_cstr(cases[index].request), &diag),
                    SL_STATUS_OK) != 0 ||
                server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY)
            {
                stop_one_connection(&server, &client);
                return 450;
            }
            if (expect_status(sl_http_transport_connection_close(&server.connections[0], &diag),
                              SL_STATUS_OK) != 0 ||
                server.connections[0].last_diag.code != SL_DIAG_HTTP_CHUNK_FINAL_MISSING)
            {
                stop_one_connection(&server, &client);
                return 451;
            }
            stop_one_connection(&server, &client);
            continue;
        }
        if (expect_status(sl_http_transport_connection_feed_test(
                              &server.connections[0], bytes_from_cstr(cases[index].request), &diag),
                          cases[index].status_code) != 0 ||
            diag.code != cases[index].diag_code)
        {
            stop_one_connection(&server, &client);
            return 460 + (int)index;
        }
        stop_one_connection(&server, &client);
    }
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
                          bytes_from_cstr("POST / HTTP/1.1\r\nHost: example.test\r\n"
                                          "Transfer-Encoding: gzip\r\n\r\n"),
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
                          bytes_from_cstr("POST / HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: text/plain\r\n"
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
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("POST / HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: application/x-unsupported\r\n"
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
                          bytes_from_cstr("GET /one HTTP/1.1\r\nHost: example.test\r\n\r\nGET /two "
                                          "HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                          &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_HTTP_PIPELINING_UNSUPPORTED)
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
                          bytes_from_cstr("POST /body HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: text/plain\r\n"
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

    if (expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /again HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                          &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        dispatch.count != 1U)
    {
        stop_one_connection(&server, &client);
        return 81;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_streaming_response_writes_chunked_frames(void)
{
    static const char expected[] =
        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\n"
        "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    SlHttpResponseStreamChunk chunks[1] = {};
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    DispatchHook dispatch = {};
    SlBytes response = {};

    chunks[0].bytes = bytes_from_cstr("hello");
    dispatch.response =
        sl_http_response_stream(200U, sl_str_from_cstr("text/plain; charset=utf-8"), chunks, 1U);
    config = small_config(nullptr);
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;

    if (run_localhost_request(&config, "GET /stream HTTP/1.1\r\nHost: local\r\n\r\n", &response,
                              &server, &dispatch) != 0 ||
        dispatch.count != 1U || !server.connections[0].write_completed ||
        expect_bytes_equal(response, expected) != 0)
    {
        return 810;
    }
    return 0;
}

static int test_streaming_response_multiple_empty_and_keep_alive(void)
{
    static const char first_expected[] =
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n";
    static const char second_expected[] =
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    SequenceDispatchHook dispatch = {};
    SlDiag diag = {};

    dispatch.stream_chunks[0].bytes = bytes_from_cstr("hello");
    dispatch.stream_chunks[1].bytes = bytes_from_cstr("world");
    dispatch.stream_response = sl_http_response_stream(
        200U, sl_str_from_cstr("text/plain; charset=utf-8"), dispatch.stream_chunks, 2U);
    dispatch.use_stream_response = true;
    config = keep_alive_config(nullptr);
    config.dispatch = sequence_dispatch_hook;
    config.dispatch_user = &dispatch;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        write_client_bytes(&client, "GET /stream-a HTTP/1.1\r\nHost: local\r\n\r\n") != 0 ||
        poll_until_connection_state(&server, &client,
                                    SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE,
                                    sizeof(first_expected) - 1U) != 0 ||
        dispatch.count != 1U || server.backend.active_requests != 0U ||
        server.backend.active_connections != 1U)
    {
        stop_one_connection(&server, &client);
        return 814;
    }

    dispatch.stream_response = sl_http_response_stream(
        200U, sl_str_from_cstr("text/plain; charset=utf-8"), dispatch.stream_chunks, 0U);
    if (write_client_bytes(&client, "GET /stream-empty HTTP/1.1\r\nHost: local\r\n\r\n") != 0 ||
        poll_until_connection_state(
            &server, &client, SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE,
            (sizeof(first_expected) - 1U) + (sizeof(second_expected) - 1U)) != 0 ||
        dispatch.count != 2U || server.connections[0].core.request_count != 2U ||
        server.connections[0].streaming_response ||
        server.connections[0].stream_chunk_index != 0U ||
        server.connections[0].response_length != 0U)
    {
        stop_one_connection(&server, &client);
        return 815;
    }

    if (expect_bytes_equal(sl_bytes_from_parts(client.read_buffer, client.read_length),
                           "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: "
                           "text/plain; charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n"
                           "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: "
                           "text/plain; charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "0\r\n\r\n") != 0)
    {
        stop_one_connection(&server, &client);
        return 816;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_streaming_response_rejects_non_arena_chunks(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    ClientConnect client = {};
    DispatchHook dispatch = {};
    SlDiag diag = {};

    config = small_config(nullptr);
    config.dispatch = stack_stream_dispatch_hook;
    config.dispatch_user = &dispatch;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /stream HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                          &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        dispatch.count != 1U ||
        server.connections[0].last_diag.code != SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED)
    {
        stop_one_connection(&server, &client);
        return 813;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_streaming_response_backpressure_rejection(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    DispatchHook dispatch = {};
    SlHttpResponseStreamChunk chunks[1] = {};
    SlDiag diag = {};

    chunks[0].bytes = bytes_from_cstr(
        "too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-"
        "too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-"
        "too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-"
        "too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-"
        "too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-large-too-"
        "large");
    dispatch.response =
        sl_http_response_stream(200U, sl_str_from_cstr("text/plain; charset=utf-8"), chunks, 1U);
    config = small_config(nullptr);
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;
    config.max_pending_write_bytes = 256U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /stream HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        stop_one_connection(&server, &client);
        return 811;
    }
    for (size_t index = 0U; index < 64U && server.backend.active_requests != 0U; index += 1U) {
        (void)sl_http_transport_server_poll(&server, &diag);
    }
    if (server.connections[0].last_diag.code != SL_DIAG_HTTP_RESPONSE_BACKPRESSURE ||
        server.backend.active_requests != 0U)
    {
        stop_one_connection(&server, &client);
        return 812;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int poll_until_connection_state(SlHttpTransportServer* server, ClientConnect* client,
                                       SlHttpTransportConnectionState state, size_t min_read)
{
    SlDiag diag = {};

    for (size_t index = 0U; index < 512U; index += 1U) {
        if (expect_status(sl_http_transport_server_poll(server, &diag), SL_STATUS_OK) != 0) {
            return 1;
        }
        (void)uv_run(&client->loop, UV_RUN_NOWAIT);
        if (server->connections[0].state == state && client->read_length >= min_read) {
            return 0;
        }
        uv_sleep(1U);
    }
    return 2;
}

static int test_keep_alive_two_sequential_requests_reuse_connection(void)
{
    static const char first_response[] =
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 6\r\n\r\nhello\n";
    static const char second_response[] =
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 7\r\n\r\nsecond\n";
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    DispatchHook dispatch = {};
    SlDiag diag = {};

    config = keep_alive_config(nullptr);
    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("hello\n"));
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 820;
    }
    if (expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 821;
    }
    if (expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0) {
        return 822;
    }
    if (connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0)
    {
        stop_one_connection(&server, &client);
        return 823;
    }
    if (write_client_bytes(&client, "GET /first HTTP/1.1\r\nHost: local\r\n\r\n") != 0) {
        stop_one_connection(&server, &client);
        return 824;
    }
    if (poll_until_connection_state(&server, &client,
                                    SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE,
                                    sizeof(first_response) - 1U) != 0)
    {
        stop_one_connection(&server, &client);
        return 825;
    }
    if (dispatch.count != 1U || server.backend.active_requests != 0U ||
        server.backend.active_connections != 1U)
    {
        stop_one_connection(&server, &client);
        return 826;
    }

    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("second\n"));
    if (write_client_bytes(&client, "GET /second HTTP/1.1\r\nHost: local\r\n\r\n") != 0 ||
        poll_until_connection_state(
            &server, &client, SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE,
            (sizeof(first_response) - 1U) + (sizeof(second_response) - 1U)) != 0 ||
        dispatch.count != 2U || server.connections[0].core.request_count != 2U ||
        server.connections[0].request_started || server.backend.active_requests != 0U ||
        server.backend.active_connections != 1U)
    {
        stop_one_connection(&server, &client);
        return 83;
    }

    if (expect_bytes_equal(sl_bytes_from_parts(client.read_buffer, client.read_length),
                           "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: "
                           "text/plain; charset=utf-8\r\nContent-Length: 6\r\n\r\nhello\n"
                           "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: "
                           "text/plain; charset=utf-8\r\nContent-Length: 7\r\n\r\nsecond\n") != 0)
    {
        stop_one_connection(&server, &client);
        return 84;
    }

    if (server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE) {
        stop_one_connection(&server, &client);
        return 89;
    }
    if (expect_status(sl_http_transport_server_stop(&server, &diag), SL_STATUS_OK) != 0) {
        close_client(&client);
        return 85;
    }
    if (server.shutdown_idle_closes != 1U) {
        close_client(&client);
        return 86;
    }
    if (sl_http_transport_server_active_connections(&server) != 0U ||
        server.backend.active_requests != 0U)
    {
        close_client(&client);
        return 87;
    }
    if (expect_status(sl_http_transport_server_dispose(&server, &diag), SL_STATUS_OK) != 0) {
        close_client(&client);
        return 88;
    }

    close_client(&client);
    return 0;
}

static int test_keep_alive_n_post_get_and_lifecycle_reset(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 3\r\n\r\nok\n";
    static const char expected[] =
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 3\r\n\r\nok\n"
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 3\r\n\r\nok\n"
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 3\r\n\r\nok\n"
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 3\r\n\r\nok\n";
    static const char* requests[] = {
        "POST /items/1?tag=a HTTP/1.1\r\nHost: local\r\nX-Test: first\r\n"
        "Content-Type: text/plain\r\nContent-Length: 5\r\n\r\nfirst",
        "GET /items/2?tag=b HTTP/1.1\r\nHost: local\r\nX-Test: second\r\n\r\n",
        "POST /items/3?tag=c HTTP/1.1\r\nHost: local\r\nX-Test: third\r\n"
        "Content-Type: text/plain\r\nContent-Length: 6\r\n\r\nsecond",
        "GET /items/4?tag=d HTTP/1.1\r\nHost: local\r\nX-Test: fourth\r\n\r\n",
    };
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    SequenceDispatchHook dispatch = {};
    SlDiag diag = {};

    config = keep_alive_config(nullptr);
    config.max_requests_per_connection = 8U;
    config.dispatch = sequence_dispatch_hook;
    config.dispatch_user = &dispatch;
    dispatch.expected_paths[0] = "/items/1";
    dispatch.expected_request_bodies[0] = "first";
    dispatch.expected_paths[1] = "/items/2";
    dispatch.expected_paths[2] = "/items/3";
    dispatch.expected_request_bodies[2] = "second";
    dispatch.expected_paths[3] = "/items/4";

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0)
    {
        stop_one_connection(&server, &client);
        return 830;
    }

    for (size_t index = 0U; index < sizeof(requests) / sizeof(requests[0]); index += 1U) {
        if (write_client_bytes(&client, requests[index]) != 0 ||
            poll_until_connection_state(&server, &client,
                                        SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE,
                                        (index + 1U) * (sizeof(response) - 1U)) != 0 ||
            dispatch.count != index + 1U || dispatch.mismatch ||
            server.backend.active_requests != 0U || server.backend.active_connections != 1U ||
            server.connections[0].request_started || server.connections[0].response_length != 0U)
        {
            stop_one_connection(&server, &client);
            return 831 + (int)index;
        }
    }

    if (dispatch.methods[0] != SL_HTTP_METHOD_POST || dispatch.methods[1] != SL_HTTP_METHOD_GET ||
        dispatch.methods[2] != SL_HTTP_METHOD_POST || dispatch.methods[3] != SL_HTTP_METHOD_GET ||
        server.connections[0].core.request_count != 4U)
    {
        stop_one_connection(&server, &client);
        return 840;
    }

    if (expect_bytes_equal(sl_bytes_from_parts(client.read_buffer, client.read_length), expected) !=
        0)
    {
        stop_one_connection(&server, &client);
        return 841;
    }

    stop_one_connection(&server, &client);
    return 0;
}

static int test_keep_alive_close_disabled_http10_and_max_requests(void)
{
    static const char close_response[] =
        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 3\r\n\r\nok\n";

    for (size_t index = 0U; index < 4U; index += 1U) {
        SlHttpTransportConfig config = keep_alive_config(nullptr);
        DispatchHook dispatch = {};
        SlHttpTransportServer server = {};
        SlBytes response = {};

        dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("ok\n"));
        config.dispatch = dispatch_hook;
        config.dispatch_user = &dispatch;
        if (index == 0U) {
            config.keep_alive_disabled = true;
        }
        if (index == 3U) {
            config.max_requests_per_connection = 1U;
        }

        const char* request = index == 1U   ? "GET /close HTTP/1.1\r\nHost: local\r\nConnection: "
                                              "close\r\n\r\n"
                              : index == 2U ? "GET /old HTTP/1.0\r\nHost: local\r\n\r\n"
                                            : "GET /ok HTTP/1.1\r\nHost: local\r\n\r\n";
        if (run_localhost_request(&config, request, &response, &server, &dispatch) != 0 ||
            expect_bytes_equal(response, close_response) != 0)
        {
            return 85 + (int)index;
        }
        if (index == 1U && server.client_close_requests != 1U) {
            return 90;
        }
        if (index == 3U && server.max_requests_reached != 1U) {
            return 91;
        }
    }
    return 0;
}

static int test_keep_alive_idle_timeout_closes_once(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    DispatchHook dispatch = {};
    SlDiag diag = {};

    config = keep_alive_config(nullptr);
    config.keep_alive_idle_timeout_ms = 25U;
    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("ok\n"));
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0 ||
        write_client_bytes(&client, "GET /idle HTTP/1.1\r\nHost: local\r\n\r\n") != 0)
    {
        stop_one_connection(&server, &client);
        return 92;
    }

    for (size_t poll = 0U; poll < 512U && server.backend.active_connections != 0U; poll += 1U) {
        if (expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0) {
            stop_one_connection(&server, &client);
            return 93;
        }
        (void)uv_run(&client.loop, UV_RUN_NOWAIT);
        uv_sleep(1U);
    }
    if (server.idle_timeouts != 1U || server.backend.active_connections != 0U ||
        server.backend.active_requests != 0U)
    {
        stop_one_connection(&server, &client);
        return 94;
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
            expect_status(sl_http_transport_connection_feed_test(
                              &server.connections[0],
                              bytes_from_cstr("GET /mapped HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                              &diag),
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

static int test_localhost_transport_smoke_success_and_dispatch_statuses(void)
{
    static const char success[] =
        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 6\r\n\r\nhello\n";
    static const char created[] =
        "HTTP/1.1 201 Created\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 8\r\n\r\ncreated\n";
    static const char not_found[] =
        "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 10\r\n\r\nNot Found\n";
    static const char method_not_allowed[] =
        "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 19\r\n\r\nMethod Not Allowed\n";
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    DispatchHook dispatch = {};
    SlBytes response = {};

    config = small_config(nullptr);
    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("hello\n"));
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;
    if (run_localhost_request(&config, "GET /ok HTTP/1.1\r\nHost: local\r\n\r\n", &response,
                              &server, &dispatch) != 0 ||
        dispatch.method != SL_HTTP_METHOD_GET || expect_str_equal(dispatch.path, "/ok") != 0 ||
        !server.connections[0].write_completed || expect_bytes_equal(response, success) != 0)
    {
        return 150;
    }

    config = small_config(nullptr);
    dispatch = {};
    dispatch.response = sl_http_response_text(201U, sl_str_from_cstr("created\n"));
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;
    if (run_localhost_request(&config,
                              "POST /text HTTP/1.1\r\nHost: local\r\nContent-Type: text/plain\r\n"
                              "Content-Length: 5\r\n\r\nhello",
                              &response, &server, &dispatch) != 0 ||
        dispatch.method != SL_HTTP_METHOD_POST || expect_str_equal(dispatch.path, "/text") != 0 ||
        expect_bytes_equal(dispatch.body, "hello") != 0 ||
        expect_bytes_equal(response, created) != 0)
    {
        return 151;
    }

    config = small_config(nullptr);
    dispatch = {};
    dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("hello\n"));
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;
    if (run_localhost_request(&config,
                              "GET /keep HTTP/1.1\r\nHost: local\r\nConnection: "
                              "keep-alive\r\n\r\n",
                              &response, &server, &dispatch) != 0 ||
        dispatch.method != SL_HTTP_METHOD_GET || expect_str_equal(dispatch.path, "/keep") != 0 ||
        expect_bytes_equal(response, success) != 0)
    {
        return 152;
    }

    config = small_config(nullptr);
    dispatch = {};
    dispatch.status_code = SL_STATUS_OUT_OF_RANGE;
    dispatch.diag_code = SL_DIAG_HTTP_ROUTE_NOT_FOUND;
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;
    if (run_localhost_request(&config, "GET /missing HTTP/1.1\r\nHost: local\r\n\r\n", &response,
                              &server, &dispatch) != 0 ||
        dispatch.count != 1U ||
        server.connections[0].last_diag.code != SL_DIAG_HTTP_ROUTE_NOT_FOUND ||
        expect_bytes_equal(response, not_found) != 0)
    {
        return 153;
    }

    config = small_config(nullptr);
    dispatch = {};
    dispatch.status_code = SL_STATUS_UNSUPPORTED;
    dispatch.diag_code = SL_DIAG_HTTP_UNSUPPORTED_METHOD;
    config.dispatch = dispatch_hook;
    config.dispatch_user = &dispatch;
    if (run_localhost_request(&config,
                              "POST /ok HTTP/1.1\r\nHost: local\r\nContent-Length: "
                              "0\r\n\r\n",
                              &response, &server, &dispatch) != 0 ||
        dispatch.count != 1U ||
        server.connections[0].last_diag.code != SL_DIAG_HTTP_UNSUPPORTED_METHOD ||
        expect_bytes_equal(response, method_not_allowed) != 0)
    {
        return 154;
    }

    return 0;
}

static int test_localhost_transport_smoke_body_success_and_rejections(void)
{
    static const char malformed[] =
        "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 23\r\n\r\nMalformed HTTP request\n";
    static const char too_large[] =
        "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 18\r\n\r\nPayload Too Large\n";
    static const char unsupported_media[] =
        "HTTP/1.1 415 Unsupported Media Type\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 23\r\n\r\nUnsupported Media Type\n";
    static const char unsupported_body[] =
        "HTTP/1.1 501 Not Implemented\r\nConnection: close\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 38\r\n\r\nRequest body framing is not supported\n";
    SlHttpTransportConfig config = {};
    SlHttpTransportServer server = {};
    SlBytes response = {};

    config = small_config(nullptr);
    if (run_localhost_request(&config, "GET / HTTP/1.1\r\nBad\r\n\r\n", &response, &server,
                              nullptr) != 0 ||
        server.connections[0].last_diag.code != SL_DIAG_INVALID_HTTP_REQUEST ||
        expect_bytes_equal(response, malformed) != 0)
    {
        return 160;
    }

    config = small_config(nullptr);
    if (run_localhost_request(&config,
                              "POST / HTTP/1.1\r\nHost: local\r\nContent-Type: text/plain\r\n"
                              "Content-Length: 33\r\n\r\n",
                              &response, &server, nullptr) != 0 ||
        server.connections[0].last_diag.code != SL_DIAG_HTTP_BODY_LIMIT ||
        expect_bytes_equal(response, too_large) != 0)
    {
        return 161;
    }

    config = small_config(nullptr);
    if (run_localhost_request(&config,
                              "POST / HTTP/1.1\r\nHost: local\r\n"
                              "Content-Type: application/x-unsupported\r\n"
                              "Content-Length: 2\r\n\r\nhi",
                              &response, &server, nullptr) != 0 ||
        server.connections[0].last_diag.code != SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE ||
        expect_bytes_equal(response, unsupported_media) != 0)
    {
        return 162;
    }

    config = small_config(nullptr);
    if (run_localhost_request(&config,
                              "POST / HTTP/1.1\r\nHost: local\r\nTransfer-Encoding: gzip\r\n"
                              "\r\n",
                              &response, &server, nullptr) != 0)
    {
        return 163;
    }
    if (server.connections[0].last_diag.code != SL_DIAG_HTTP_UNSUPPORTED_BODY) {
        return 164;
    }
    if (expect_bytes_equal(response, unsupported_body) != 0) {
        return 165;
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
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /big HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                          &diag),
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
                          bytes_from_cstr("POST /body HTTP/1.1\r\nHost: example.test\r\n"
                                          "Content-Type: text/plain\r\n"
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
            if (write_client_bytes(&client, "POST /slow HTTP/1.1\r\nHost: example.test\r\n"
                                            "Content-Type: text/plain\r\n"
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
        expect_status(sl_http_transport_connection_feed_test(
                          &server.connections[0],
                          bytes_from_cstr("GET /write HTTP/1.1\r\nHost: example.test\r\n\r\n"),
                          &diag),
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

static int test_bounded_keep_alive_chunked_streaming_stress_smoke(void)
{
    static const char keep_alive_response[] =
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 3\r\n\r\nok\n";
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    ClientConnect client = {};
    SequenceDispatchHook dispatch = {};
    SlDiag diag = {};

    config = keep_alive_config(nullptr);
    config.max_requests_per_connection = 16U;
    config.dispatch = sequence_dispatch_hook;
    config.dispatch_user = &dispatch;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
        start_client_read(&client) != 0 ||
        expect_status(sl_http_transport_server_poll(&server, &diag), SL_STATUS_OK) != 0)
    {
        stop_one_connection(&server, &client);
        return 220;
    }
    for (size_t index = 0U; index < 8U; index += 1U) {
        if (write_client_bytes(&client, "GET /stress HTTP/1.1\r\nHost: local\r\n\r\n") != 0 ||
            poll_until_connection_state(&server, &client,
                                        SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE,
                                        (index + 1U) * (sizeof(keep_alive_response) - 1U)) != 0 ||
            server.backend.active_requests != 0U || server.backend.active_connections != 1U)
        {
            stop_one_connection(&server, &client);
            return 221 + (int)index;
        }
    }
    if (dispatch.count != 8U || server.connections[0].core.request_count != 8U) {
        stop_one_connection(&server, &client);
        return 230;
    }
    stop_one_connection(&server, &client);

    for (size_t index = 0U; index < 6U; index += 1U) {
        SlHttpTransportServer short_server = {};
        SlBytes response = {};
        DispatchHook short_dispatch = {};
        SlHttpTransportConfig short_config = keep_alive_config(nullptr);

        short_dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("ok\n"));
        short_config.dispatch = dispatch_hook;
        short_config.dispatch_user = &short_dispatch;
        if (run_localhost_request(&short_config,
                                  "GET /short HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n",
                                  &response, &short_server, &short_dispatch) != 0 ||
            short_server.backend.active_requests != 0U ||
            sl_http_transport_server_active_connections(&short_server) != 0U)
        {
            return 240 + (int)index;
        }
    }

    for (size_t index = 0U; index < 6U; index += 1U) {
        SlHttpTransportServer chunked_server = {};
        SlBytes response = {};
        DispatchHook chunked_dispatch = {};
        SlHttpTransportConfig chunked_config = small_config(nullptr);

        chunked_dispatch.response = sl_http_response_text(200U, sl_str_from_cstr("ok\n"));
        chunked_config.dispatch = dispatch_hook;
        chunked_config.dispatch_user = &chunked_dispatch;
        if (run_localhost_request(
                &chunked_config,
                "POST /chunk HTTP/1.1\r\nHost: local\r\nContent-Type: text/plain\r\n"
                "Transfer-Encoding: chunked\r\n\r\n2\r\nhi\r\n0\r\n\r\n",
                &response, &chunked_server, &chunked_dispatch) != 0 ||
            expect_bytes_equal(chunked_dispatch.body, "hi") != 0 ||
            chunked_server.backend.active_requests != 0U)
        {
            return 250 + (int)index;
        }
    }

    for (size_t index = 0U; index < 6U; index += 1U) {
        SlHttpTransportServer stream_server = {};
        SlBytes response = {};
        SlHttpResponseStreamChunk chunks[1] = {};
        DispatchHook stream_dispatch = {};
        SlHttpTransportConfig stream_config = small_config(nullptr);

        chunks[0].bytes = bytes_from_cstr("hi");
        stream_dispatch.response =
            sl_http_response_stream(200U, sl_str_from_cstr("text/plain"), chunks, 1U);
        stream_config.dispatch = dispatch_hook;
        stream_config.dispatch_user = &stream_dispatch;
        if (run_localhost_request(&stream_config, "GET /stream HTTP/1.1\r\nHost: local\r\n\r\n",
                                  &response, &stream_server, &stream_dispatch) != 0 ||
            stream_server.backend.active_requests != 0U)
        {
            return 260 + (int)index;
        }
    }

    for (size_t index = 0U; index < 6U; index += 1U) {
        SlHttpTransportServer malformed_server = {};
        SlBytes response = {};
        SlHttpTransportConfig malformed_config = small_config(nullptr);

        if (run_localhost_request(&malformed_config, "GET / HTTP/1.1\r\nBad\r\n\r\n", &response,
                                  &malformed_server, nullptr) != 0 ||
            malformed_server.connections[0].last_diag.code != SL_DIAG_INVALID_HTTP_REQUEST ||
            malformed_server.backend.active_requests != 0U ||
            sl_http_transport_server_active_connections(&malformed_server) != 0U)
        {
            return 270 + (int)index;
        }
    }

    {
        unsigned char shutdown_storage[65536];
        SlArena shutdown_arena = {};
        SlHttpTransportServer shutdown_server = {};
        SlHttpTransportConfig shutdown_config = {};
        ClientConnect shutdown_client = {};

        shutdown_config = keep_alive_config(nullptr);
        shutdown_config.dispatch = sequence_dispatch_hook;
        shutdown_config.dispatch_user = &dispatch;
        dispatch = {};
        if (expect_status(
                sl_arena_init(&shutdown_arena, shutdown_storage, sizeof(shutdown_storage)),
                SL_STATUS_OK) != 0 ||
            expect_status(sl_http_transport_server_init(&shutdown_server, &shutdown_arena,
                                                        &shutdown_config, &diag),
                          SL_STATUS_OK) != 0 ||
            expect_status(sl_http_transport_server_listen(&shutdown_server, &diag), SL_STATUS_OK) !=
                0 ||
            connect_client(sl_http_transport_server_bound_port(&shutdown_server),
                           &shutdown_client) != 0 ||
            start_client_read(&shutdown_client) != 0 ||
            expect_status(sl_http_transport_server_poll(&shutdown_server, &diag), SL_STATUS_OK) !=
                0 ||
            write_client_bytes(&shutdown_client, "GET /shutdown HTTP/1.1\r\nHost: local\r\n\r\n") !=
                0 ||
            poll_until_connection_state(&shutdown_server, &shutdown_client,
                                        SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE,
                                        sizeof(keep_alive_response) - 1U) != 0 ||
            expect_status(sl_http_transport_server_stop(&shutdown_server, &diag), SL_STATUS_OK) !=
                0 ||
            shutdown_server.shutdown_idle_closes != 1U ||
            shutdown_server.backend.active_requests != 0U ||
            sl_http_transport_server_active_connections(&shutdown_server) != 0U)
        {
            stop_one_connection(&shutdown_server, &shutdown_client);
            return 280;
        }
        stop_one_connection(&shutdown_server, &shutdown_client);
    }

    return 0;
}

typedef int (*TransportCaseTest)(void);

static int run_case_sequence(const TransportCaseTest* tests, size_t test_count)
{
    for (size_t index = 0U; index < test_count; index += 1U) {
        int result = tests[index]();
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

#define SLOPPY_TRANSPORT_CASE_SEQUENCE(...)                                                        \
    do {                                                                                           \
        static const TransportCaseTest tests[] = {__VA_ARGS__};                                    \
        return run_case_sequence(tests, sizeof(tests) / sizeof(tests[0]));                         \
    } while (0)

static int run_named_transport_case(const char* name)
{
    if (name == nullptr) {
        return 2;
    }
    if (strcmp(name, "localhost_mvp") == 0) {
        SLOPPY_TRANSPORT_CASE_SEQUENCE(test_localhost_transport_smoke_success_and_dispatch_statuses,
                                       test_localhost_transport_smoke_body_success_and_rejections,
                                       test_dispatch_failures_map_to_safe_responses);
    }
    if (strcmp(name, "keep_alive") == 0) {
        return test_keep_alive_two_sequential_requests_reuse_connection();
    }
    if (strcmp(name, "keep_alive_idle_timeout") == 0) {
        return test_keep_alive_idle_timeout_closes_once();
    }
    if (strcmp(name, "keep_alive_max_requests") == 0) {
        return test_keep_alive_close_disabled_http10_and_max_requests();
    }
    if (strcmp(name, "lifecycle_reset") == 0) {
        return test_keep_alive_n_post_get_and_lifecycle_reset();
    }
    if (strcmp(name, "chunked_request") == 0) {
        SLOPPY_TRANSPORT_CASE_SEQUENCE(test_chunked_request_decoding_success_cases,
                                       test_chunked_request_decoding_allows_bounded_wire_overhead,
                                       test_chunked_request_decoding_rejections);
    }
    if (strcmp(name, "streaming_response") == 0) {
        SLOPPY_TRANSPORT_CASE_SEQUENCE(test_streaming_response_writes_chunked_frames,
                                       test_streaming_response_multiple_empty_and_keep_alive);
    }
    if (strcmp(name, "backpressure") == 0) {
        SLOPPY_TRANSPORT_CASE_SEQUENCE(test_streaming_response_backpressure_rejection,
                                       test_response_buffer_capacity_failure_is_deterministic);
    }
    if (strcmp(name, "shutdown_cancel") == 0) {
        SLOPPY_TRANSPORT_CASE_SEQUENCE(test_disconnect_cleanup_paths,
                                       test_shutdown_rejects_new_accepts_and_closes_head_read,
                                       test_shutdown_during_body_read_cancels_cleanup_once,
                                       test_header_and_body_timeout_write_408_or_close,
                                       test_shutdown_during_response_write_is_cleanup_only);
    }
    return 2;
}

int main(int argc, char** argv)
{
    bool run_core = true;
    bool run_bounded_smoke = true;
    if (argc == 3 && strcmp(argv[1], "--case") == 0) {
        return run_named_transport_case(argv[2]);
    }
    if (argc == 2 && strcmp(argv[1], "--without-bounded-smoke") == 0) {
        run_bounded_smoke = false;
    }
    else if (argc == 2 && strcmp(argv[1], "--bounded-smoke-only") == 0) {
        run_core = false;
    }
    else if (argc != 1) {
        return 2;
    }
    if (!run_core) {
        return test_bounded_keep_alive_chunked_streaming_stress_smoke();
    }

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
    result = test_chunked_request_decoding_success_cases();
    if (result != 0) {
        return result;
    }
    result = test_chunked_request_decoding_allows_bounded_wire_overhead();
    if (result != 0) {
        return result;
    }
    result = test_chunked_request_decoding_rejections();
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
    result = test_streaming_response_writes_chunked_frames();
    if (result != 0) {
        return result;
    }
    result = test_streaming_response_multiple_empty_and_keep_alive();
    if (result != 0) {
        return result;
    }
    result = test_streaming_response_rejects_non_arena_chunks();
    if (result != 0) {
        return result;
    }
    result = test_streaming_response_backpressure_rejection();
    if (result != 0) {
        return result;
    }
    result = test_keep_alive_two_sequential_requests_reuse_connection();
    if (result != 0) {
        return result;
    }
    result = test_keep_alive_n_post_get_and_lifecycle_reset();
    if (result != 0) {
        return result;
    }
    result = test_keep_alive_close_disabled_http10_and_max_requests();
    if (result != 0) {
        return result;
    }
    result = test_keep_alive_idle_timeout_closes_once();
    if (result != 0) {
        return result;
    }
    result = test_dispatch_failures_map_to_safe_responses();
    if (result != 0) {
        return result;
    }
    result = test_localhost_transport_smoke_success_and_dispatch_statuses();
    if (result != 0) {
        return result;
    }
    result = test_localhost_transport_smoke_body_success_and_rejections();
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
    result = test_shutdown_during_response_write_is_cleanup_only();
    if (result != 0) {
        return result;
    }
    if (run_bounded_smoke) {
        return test_bounded_keep_alive_chunked_streaming_stress_smoke();
    }
    return 0;
}
