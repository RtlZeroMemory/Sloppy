#include "sloppy/arena.h"
#include "sloppy/net.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <thread>

namespace {

int expect_status(SlStatus status, SlStatusCode expected)
{
    return sl_status_code(status) == expected ? 0 : 1;
}

std::string unique_pipe_path(const char* name)
{
    return std::string("\\\\.\\pipe\\sloppy-local-ipc-") +
           std::to_string(static_cast<unsigned long long>(GetCurrentProcessId())) + "-" + name;
}

struct ClientExchange
{
    unsigned char storage[64U * 1024U] = {};
    SlArena arena = {};
    std::string path;
    int result = 0;
};

void run_client(ClientExchange* exchange)
{
    SlLocalConnection* connection = nullptr;
    SlLocalConnectOptions options = {};
    SlOwnedBytes bytes = {};
    const unsigned char binary[] = {'p', '\0', 'q', '\n'};

    if (exchange == nullptr) {
        return;
    }
    sl_arena_init(&exchange->arena, exchange->storage, sizeof(exchange->storage));
    options = sl_local_connect_options_default(sl_str_from_cstr(exchange->path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    options.has_timeout_ms = true;
    options.timeout_ms = 2000U;
    if (expect_status(sl_local_endpoint_connect(&exchange->arena, &options, &connection, nullptr),
                      SL_STATUS_OK) != 0 ||
        connection == nullptr)
    {
        exchange->result = 1;
        return;
    }
    if (expect_status(sl_local_connection_write(
                          connection, sl_bytes_from_parts(binary, sizeof(binary)), nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_local_connection_read(connection, &exchange->arena, sizeof(binary), &bytes, nullptr),
            SL_STATUS_OK) != 0 ||
        bytes.length != sizeof(binary) || std::memcmp(bytes.ptr, binary, sizeof(binary)) != 0)
    {
        exchange->result = 2;
        (void)sl_local_connection_abort(connection, nullptr);
        return;
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0) {
        exchange->result = 3;
        return;
    }
}

int test_named_pipe_loopback_preserves_binary()
{
    unsigned char server_storage[32U * 1024U] = {};
    unsigned char accepted_storage[64U * 1024U] = {};
    SlArena server_arena = {};
    SlArena accepted_arena = {};
    SlLocalServer* server = nullptr;
    SlLocalConnection* accepted = nullptr;
    SlLocalListenOptions listen_options = {};
    SlLocalAcceptOptions accept_options = {};
    SlOwnedBytes bytes = {};
    ClientExchange exchange = {};
    std::string path = unique_pipe_path("loopback");

    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0 ||
        server == nullptr || sl_local_server_state(server) != SL_LOCAL_SERVER_LISTENING)
    {
        return 1;
    }
    exchange.path = path;
    std::thread client(run_client, &exchange);
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 2000U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr || sl_local_connection_state(accepted) != SL_LOCAL_CONNECTION_CONNECTED)
    {
        client.join();
        (void)sl_local_server_abort(server, nullptr);
        return 2;
    }
    if (expect_status(sl_local_connection_read(accepted, &accepted_arena, 4U, &bytes, nullptr),
                      SL_STATUS_OK) != 0 ||
        bytes.length != 4U || std::memcmp(bytes.ptr, "p\0q\n", 4U) != 0 ||
        expect_status(sl_local_connection_write(accepted, sl_owned_bytes_as_view(bytes), nullptr),
                      SL_STATUS_OK) != 0)
    {
        client.join();
        (void)sl_local_connection_abort(accepted, nullptr);
        (void)sl_local_server_abort(server, nullptr);
        return 3;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        return 4;
    }
    return 0;
}

int test_named_pipe_policy_and_unsupported_backend()
{
    unsigned char server_storage[32U * 1024U] = {};
    SlArena server_arena = {};
    SlLocalServer* server = nullptr;
    SlLocalServer* duplicate = nullptr;
    SlLocalListenOptions options = {};
    std::string path = unique_pipe_path("policy");

    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 1;
    }
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    options.has_permissions = true;
    options.permissions = 0600U;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 2;
    }
    options.has_permissions = false;
    options.unlink_existing = true;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 3;
    }
    options.unlink_existing = false;
    options.path = sl_str_from_cstr("runtime:/not-a-pipe");
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 4;
    }
    options.path = sl_str_from_cstr(path.c_str());
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_local_endpoint_listen(&server_arena, &options, &duplicate, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        (void)sl_local_server_abort(server, nullptr);
        return 5;
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0) {
        return 6;
    }
    return 0;
}

int test_accept_timeout_and_disposal()
{
    unsigned char server_storage[32U * 1024U] = {};
    unsigned char accepted_storage[32U * 1024U] = {};
    SlArena server_arena = {};
    SlArena accepted_arena = {};
    SlLocalServer* server = nullptr;
    SlLocalConnection* accepted = nullptr;
    SlLocalListenOptions listen_options = {};
    SlLocalAcceptOptions accept_options = {};
    std::string path = unique_pipe_path("timeout");

    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 25U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        accepted != nullptr)
    {
        (void)sl_local_server_abort(server, nullptr);
        return 2;
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_accept(server, &accepted_arena, nullptr, &accepted, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 3;
    }
    return 0;
}

} // namespace

int main()
{
    if (test_named_pipe_loopback_preserves_binary() != 0) {
        return 1;
    }
    if (test_named_pipe_policy_and_unsupported_backend() != 0) {
        return 1;
    }
    if (test_accept_timeout_and_disposal() != 0) {
        return 1;
    }
    return 0;
}
