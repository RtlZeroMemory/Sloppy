#include "sloppy/arena.h"
#include "sloppy/net.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include <uv.h>

namespace {

struct EchoClient
{
    uv_tcp_t handle = {};
};

struct EchoWrite
{
    uv_write_t request = {};
    uv_buf_t buffer = {};
};

struct EchoServer
{
    uv_loop_t loop = {};
    uv_tcp_t server = {};
    uv_async_t stop = {};
    std::thread thread;
    std::atomic_bool running = false;
    uint16_t port = 0;
};

void echo_after_write(uv_write_t* request, int status)
{
    auto* write = request == nullptr ? nullptr : static_cast<EchoWrite*>(request->data);
    (void)status;
    if (write != nullptr) {
        delete[] write->buffer.base;
        delete write;
    }
}

void echo_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    (void)handle;
    if (buf == nullptr) {
        return;
    }
    size_t size = suggested_size == 0U ? 4096U : suggested_size;
    buf->base = new char[size];
    buf->len = static_cast<unsigned int>(size);
}

void echo_client_closed(uv_handle_t* handle)
{
    delete static_cast<EchoClient*>(handle == nullptr ? nullptr : handle->data);
}

void echo_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    if (nread > 0 && stream != nullptr && buf != nullptr) {
        auto* write = new EchoWrite();
        write->buffer =
            uv_buf_init(new char[static_cast<size_t>(nread)], static_cast<unsigned int>(nread));
        std::memcpy(write->buffer.base, buf->base, static_cast<size_t>(nread));
        write->request.data = write;
        (void)uv_write(&write->request, stream, &write->buffer, 1U, echo_after_write);
    }
    if (buf != nullptr) {
        delete[] buf->base;
    }
    if (nread < 0 && stream != nullptr && !uv_is_closing(reinterpret_cast<uv_handle_t*>(stream))) {
        uv_close(reinterpret_cast<uv_handle_t*>(stream), echo_client_closed);
    }
}

void echo_connection(uv_stream_t* server_stream, int status)
{
    if (status != 0 || server_stream == nullptr) {
        return;
    }
    auto* server = static_cast<EchoServer*>(server_stream->data);
    auto* client = new EchoClient();
    client->handle.data = client;
    if (uv_tcp_init(&server->loop, &client->handle) != 0 ||
        uv_accept(server_stream, reinterpret_cast<uv_stream_t*>(&client->handle)) != 0)
    {
        delete client;
        return;
    }
    (void)uv_read_start(reinterpret_cast<uv_stream_t*>(&client->handle), echo_alloc, echo_read);
}

void echo_stop(uv_async_t* async)
{
    auto* server = async == nullptr ? nullptr : static_cast<EchoServer*>(async->data);
    if (server == nullptr) {
        return;
    }
    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&server->server))) {
        uv_close(reinterpret_cast<uv_handle_t*>(&server->server), nullptr);
    }
    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&server->stop))) {
        uv_close(reinterpret_cast<uv_handle_t*>(&server->stop), nullptr);
    }
}

bool echo_server_start(EchoServer* server)
{
    sockaddr_in addr = {};
    sockaddr_storage bound = {};
    int bound_len = sizeof(bound);

    if (server == nullptr || uv_loop_init(&server->loop) != 0 ||
        uv_tcp_init(&server->loop, &server->server) != 0 || uv_ip4_addr("127.0.0.1", 0, &addr) != 0)
    {
        return false;
    }
    server->server.data = server;
    if (uv_tcp_bind(&server->server, reinterpret_cast<const sockaddr*>(&addr), 0) != 0 ||
        uv_listen(reinterpret_cast<uv_stream_t*>(&server->server), 16, echo_connection) != 0 ||
        uv_tcp_getsockname(&server->server, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
    {
        return false;
    }
    server->port = ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
    if (uv_async_init(&server->loop, &server->stop, echo_stop) != 0) {
        return false;
    }
    server->stop.data = server;
    server->thread = std::thread([server]() {
        server->running.store(true);
        (void)uv_run(&server->loop, UV_RUN_DEFAULT);
        (void)uv_loop_close(&server->loop);
        server->running.store(false);
    });
    while (!server->running.load()) {
        std::this_thread::yield();
    }
    return true;
}

void echo_server_stop(EchoServer* server)
{
    if (server == nullptr) {
        return;
    }
    (void)uv_async_send(&server->stop);
    if (server->thread.joinable()) {
        server->thread.join();
    }
}

int expect_status(SlStatus status, SlStatusCode expected)
{
    return sl_status_code(status) == expected ? 0 : 1;
}

int test_loopback_write_read_and_close()
{
    EchoServer server;
    unsigned char arena_storage[64U * 1024U];
    SlArena arena;
    SlTcpConnection* connection = nullptr;
    SlTcpConnectOptions options = {};
    SlOwnedBytes bytes = {};
    SlOwnedStr line = {};
    SlNetworkEndpoint remote = {};
    const unsigned char binary[] = {'a', '\0', 'b', '\n'};

    if (!echo_server_start(&server)) {
        return 1;
    }
    sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    options = sl_tcp_connect_options_default(sl_str_from_cstr("127.0.0.1"), server.port);
    options.no_delay = true;
    options.keep_alive.enabled = true;
    options.keep_alive.delay_ms = 30000U;

    if (expect_status(sl_tcp_client_connect(&arena, &options, &connection, nullptr),
                      SL_STATUS_OK) != 0 ||
        connection == nullptr ||
        sl_tcp_connection_state(connection) != SL_TCP_CONNECTION_CONNECTED ||
        expect_status(sl_tcp_connection_remote_endpoint(connection, &remote), SL_STATUS_OK) != 0 ||
        remote.port != server.port || remote.family != SL_NETWORK_ADDRESS_IPV4)
    {
        echo_server_stop(&server);
        return 1;
    }

    if (expect_status(sl_tcp_connection_write(connection,
                                              sl_bytes_from_parts(binary, sizeof(binary)), nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_tcp_connection_read(connection, &arena, sizeof(binary), &bytes, nullptr),
                      SL_STATUS_OK) != 0 ||
        bytes.length != sizeof(binary) || std::memcmp(bytes.ptr, binary, sizeof(binary)) != 0)
    {
        echo_server_stop(&server);
        return 1;
    }

    if (expect_status(
            sl_tcp_connection_write_text(connection, sl_str_from_cstr("PING\r\n"), nullptr),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_tcp_connection_read_line(connection, &arena, 64U, &line, nullptr),
                      SL_STATUS_OK) != 0 ||
        line.length != 4U || std::memcmp(line.ptr, "PING", 4U) != 0)
    {
        echo_server_stop(&server);
        return 1;
    }

    if (expect_status(sl_tcp_connection_close(connection, nullptr), SL_STATUS_OK) != 0 ||
        sl_tcp_connection_state(connection) != SL_TCP_CONNECTION_CLOSED ||
        expect_status(sl_tcp_connection_write_text(connection, sl_str_from_cstr("late"), nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        echo_server_stop(&server);
        return 1;
    }

    echo_server_stop(&server);
    return 0;
}

int test_invalid_host_and_port()
{
    unsigned char arena_storage[4096U];
    SlArena arena;
    SlTcpConnection* connection = nullptr;
    SlTcpConnectOptions options = {};

    sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    options = sl_tcp_connect_options_default(sl_str_from_cstr(""), 1U);
    if (expect_status(sl_tcp_client_connect(&arena, &options, &connection, nullptr),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 1;
    }
    options = sl_tcp_connect_options_default(sl_str_from_cstr("127.0.0.1"), 0U);
    if (expect_status(sl_tcp_client_connect(&arena, &options, &connection, nullptr),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 1;
    }
    return 0;
}

struct ClientExchange
{
    unsigned char arena_storage[64U * 1024U] = {};
    SlArena arena = {};
    uint16_t port = 0;
    int result = 0;
};

void client_exchange_run(ClientExchange* exchange)
{
    SlTcpConnection* connection = nullptr;
    SlTcpConnectOptions options = {};
    SlOwnedStr line = {};

    if (exchange == nullptr) {
        return;
    }
    sl_arena_init(&exchange->arena, exchange->arena_storage, sizeof(exchange->arena_storage));
    options = sl_tcp_connect_options_default(sl_str_from_cstr("127.0.0.1"), exchange->port);
    if (expect_status(sl_tcp_client_connect(&exchange->arena, &options, &connection, nullptr),
                      SL_STATUS_OK) != 0 ||
        connection == nullptr)
    {
        exchange->result = 1;
        return;
    }
    if (expect_status(
            sl_tcp_connection_write_text(connection, sl_str_from_cstr("hello-listener\n"), nullptr),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_tcp_connection_read_line(connection, &exchange->arena, 128U, &line, nullptr),
            SL_STATUS_OK) != 0 ||
        line.length != 14U || std::memcmp(line.ptr, "listener-reply", 14U) != 0)
    {
        exchange->result = 2;
        (void)sl_tcp_connection_abort(connection, nullptr);
        return;
    }
    if (expect_status(sl_tcp_connection_close(connection, nullptr), SL_STATUS_OK) != 0) {
        exchange->result = 3;
        return;
    }
    exchange->result = 0;
}

int test_listener_accept_ephemeral_and_close()
{
    unsigned char listener_storage[32U * 1024U];
    unsigned char accepted_storage[64U * 1024U];
    SlArena listener_arena = {};
    SlArena accepted_arena = {};
    SlTcpListener* listener = nullptr;
    SlTcpConnection* accepted = nullptr;
    SlTcpListenOptions listen_options = {};
    SlTcpAcceptOptions accept_options = {};
    SlNetworkEndpoint endpoint = {};
    SlOwnedStr line = {};
    ClientExchange exchange = {};

    sl_arena_init(&listener_arena, listener_storage, sizeof(listener_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_tcp_listen_options_default(sl_str_from_cstr("127.0.0.1"), 0U);
    if (expect_status(sl_tcp_listener_listen(&listener_arena, &listen_options, &listener, nullptr),
                      SL_STATUS_OK) != 0 ||
        listener == nullptr || sl_tcp_listener_state(listener) != SL_TCP_LISTENER_LISTENING ||
        expect_status(sl_tcp_listener_local_endpoint(listener, &endpoint), SL_STATUS_OK) != 0 ||
        endpoint.port == 0U || endpoint.family != SL_NETWORK_ADDRESS_IPV4)
    {
        return 1;
    }

    exchange.port = endpoint.port;
    std::thread client(client_exchange_run, &exchange);
    accept_options = sl_tcp_accept_options_default();
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 2000U;
    if (expect_status(
            sl_tcp_listener_accept(listener, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr || sl_tcp_connection_state(accepted) != SL_TCP_CONNECTION_CONNECTED)
    {
        client.join();
        (void)sl_tcp_listener_abort(listener, nullptr);
        return 2;
    }
    if (expect_status(sl_tcp_connection_read_line(accepted, &accepted_arena, 128U, &line, nullptr),
                      SL_STATUS_OK) != 0 ||
        line.length != 14U || std::memcmp(line.ptr, "hello-listener", 14U) != 0 ||
        expect_status(
            sl_tcp_connection_write_text(accepted, sl_str_from_cstr("listener-reply\n"), nullptr),
            SL_STATUS_OK) != 0)
    {
        client.join();
        (void)sl_tcp_connection_abort(accepted, nullptr);
        (void)sl_tcp_listener_abort(listener, nullptr);
        return 3;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_tcp_listener_close(listener, nullptr), SL_STATUS_INVALID_STATE) != 0 ||
        sl_tcp_listener_state(listener) != SL_TCP_LISTENER_CLOSING ||
        expect_status(sl_tcp_connection_close(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_tcp_listener_close(listener, nullptr), SL_STATUS_OK) != 0 ||
        sl_tcp_listener_state(listener) != SL_TCP_LISTENER_CLOSED)
    {
        return 4;
    }
    return 0;
}

int test_listener_accept_timeout_and_stale_handle()
{
    unsigned char listener_storage[32U * 1024U];
    unsigned char accepted_storage[32U * 1024U];
    SlArena listener_arena = {};
    SlArena accepted_arena = {};
    SlTcpListener* listener = nullptr;
    SlTcpConnection* accepted = nullptr;
    SlTcpListenOptions listen_options = {};
    SlTcpAcceptOptions accept_options = {};

    sl_arena_init(&listener_arena, listener_storage, sizeof(listener_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_tcp_listen_options_default(sl_str_from_cstr("127.0.0.1"), 0U);
    if (expect_status(sl_tcp_listener_listen(&listener_arena, &listen_options, &listener, nullptr),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 50U;
    if (expect_status(
            sl_tcp_listener_accept(listener, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        accepted != nullptr)
    {
        (void)sl_tcp_listener_abort(listener, nullptr);
        return 2;
    }
    if (expect_status(sl_tcp_listener_close(listener, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_tcp_listener_accept(listener, &accepted_arena, nullptr, &accepted, nullptr),
            SL_STATUS_INVALID_STATE) != 0)
    {
        return 3;
    }
    return 0;
}

} // namespace

int main()
{
    if (test_loopback_write_read_and_close() != 0) {
        return 1;
    }
    if (test_invalid_host_and_port() != 0) {
        return 1;
    }
    if (test_listener_accept_ephemeral_and_close() != 0) {
        return 1;
    }
    if (test_listener_accept_timeout_and_stale_handle() != 0) {
        return 1;
    }
    return 0;
}
