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

typedef struct ClientConnect
{
    uv_loop_t loop;
    uv_tcp_t handle;
    uv_connect_t request;
    int status;
    bool connected;
} ClientConnect;

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

static void close_client(ClientConnect* client)
{
    if (client == nullptr) {
        return;
    }
    uv_close((uv_handle_t*)&client->handle, client_close_cb);
    (void)uv_run(&client->loop, UV_RUN_DEFAULT);
    (void)uv_loop_close(&client->loop);
}

static int test_config_validation_and_lifecycle(void)
{
    unsigned char storage[65536];
    SlArena arena = {};
    SlHttpTransportServer server = {};
    SlHttpTransportConfig config = {};
    SlDiag diag = {};

    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = 0U;
    config.max_connections = 1U;
    config.max_active_requests = 1U;
    config.connection_capacity = 1U;
    config.backlog = 8;

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
    config.host = sl_str_from_cstr("not an address");
    server = {};
    if (expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_HTTP_TRANSPORT_CONFIG)
    {
        return 3;
    }

    sl_arena_reset(&arena);
    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = 70000U;
    server = {};
    if (expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_HTTP_TRANSPORT_CONFIG)
    {
        return 4;
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

    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = 0U;
    config.max_connections = 1U;
    config.max_active_requests = 1U;
    config.connection_capacity = 1U;
    config.backlog = 8;

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
        server.connections[0].state != SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED ||
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

    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = 0U;
    config.max_connections = 2U;
    config.max_active_requests = 2U;
    config.connection_capacity = 2U;
    config.backlog = 8;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_init(&server, &arena, &config, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_transport_server_listen(&server, &diag), SL_STATUS_OK) != 0 ||
        connect_client(sl_http_transport_server_bound_port(&server), &client) != 0 ||
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
    return test_dispose_after_listen_closes_active_connection();
}
