#include "sloppy/arena.h"
#include "sloppy/net.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <iostream>
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
    HANDLE release_event = NULL;
    int result = 0;
};

void run_idle_client(ClientExchange* exchange)
{
    SlLocalConnection* connection = nullptr;
    SlLocalConnectOptions options = {};

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
    if (exchange->release_event != NULL &&
        WaitForSingleObject(exchange->release_event, 5000U) != WAIT_OBJECT_0)
    {
        exchange->result = 2;
        sl_local_connection_abort(connection, nullptr);
        return;
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0) {
        exchange->result = 3;
    }
}

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
        sl_local_connection_abort(connection, nullptr);
        return;
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0) {
        exchange->result = 3;
        return;
    }
}

void run_split_client(ClientExchange* exchange)
{
    SlLocalConnection* connection = nullptr;
    SlLocalConnectOptions options = {};
    const unsigned char first[] = {'a', 'b', '\0'};
    const unsigned char second[] = {'c', 'd'};

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
    if (expect_status(sl_local_connection_write(connection,
                                                sl_bytes_from_parts(first, sizeof(first)), nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_local_connection_write(
                          connection, sl_bytes_from_parts(second, sizeof(second)), nullptr),
                      SL_STATUS_OK) != 0)
    {
        exchange->result = 2;
        sl_local_connection_abort(connection, nullptr);
        return;
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0) {
        exchange->result = 3;
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
        sl_local_server_abort(server, nullptr);
        return 2;
    }
    if (expect_status(sl_local_connection_read(accepted, &accepted_arena, 4U, &bytes, nullptr),
                      SL_STATUS_OK) != 0 ||
        bytes.length != 4U || std::memcmp(bytes.ptr, "p\0q\n", 4U) != 0 ||
        expect_status(sl_local_connection_write(accepted, sl_owned_bytes_as_view(bytes), nullptr),
                      SL_STATUS_OK) != 0)
    {
        client.join();
        sl_local_connection_abort(accepted, nullptr);
        sl_local_server_abort(server, nullptr);
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

int test_connect_waits_for_late_server()
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
    std::string path = unique_pipe_path("late-server");

    exchange.path = path;
    std::thread client(run_client, &exchange);
    Sleep(50U);

    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        client.join();
        return 1;
    }
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 2000U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr)
    {
        client.join();
        sl_local_server_abort(server, nullptr);
        return 2;
    }
    if (expect_status(sl_local_connection_read(accepted, &accepted_arena, 4U, &bytes, nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_local_connection_write(accepted, sl_owned_bytes_as_view(bytes), nullptr),
                      SL_STATUS_OK) != 0)
    {
        client.join();
        sl_local_connection_abort(accepted, nullptr);
        sl_local_server_abort(server, nullptr);
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
    SlLocalServer* segmented = nullptr;
    SlLocalListenOptions options = {};
    std::string path = unique_pipe_path("policy");
    std::string segmented_path = unique_pipe_path("policy-a~b");

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
    options.path = sl_str_from_cstr("\\\\.\\pipe\\bad\\nested");
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 5;
    }
    options.path = sl_str_from_cstr("\\\\.\\pipe\\bad name");
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 6;
    }
    options.path = sl_str_from_cstr(segmented_path.c_str());
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &segmented, nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(segmented, nullptr), SL_STATUS_OK) != 0)
    {
        return 7;
    }
    options.path = sl_str_from_cstr(path.c_str());
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_local_endpoint_listen(&server_arena, &options, &duplicate, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_local_server_abort(server, nullptr);
        return 8;
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0) {
        return 9;
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
        sl_local_server_abort(server, nullptr);
        return 2;
    }
    {
        SlCancellationToken token = {};

        sl_cancellation_token_init(&token);
        if (expect_status(sl_cancellation_token_cancel(&token, SL_CANCELLATION_REASON_CANCELLED,
                                                       sl_str_from_cstr("test cancellation")),
                          SL_STATUS_OK) != 0)
        {
            sl_local_server_abort(server, nullptr);
            return 3;
        }
        accept_options = sl_local_accept_options_default();
        accept_options.cancellation = &token;
        if (expect_status(sl_local_server_accept(server, &accepted_arena, &accept_options,
                                                 &accepted, nullptr),
                          SL_STATUS_CANCELLED) != 0 ||
            accepted != nullptr)
        {
            sl_local_server_abort(server, nullptr);
            return 4;
        }
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_accept(server, &accepted_arena, nullptr, &accepted, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 5;
    }
    return 0;
}

int test_io_timeout_and_precancel()
{
    unsigned char server_storage[32U * 1024U] = {};
    unsigned char accepted_storage[64U * 1024U] = {};
    SlArena server_arena = {};
    SlArena accepted_arena = {};
    SlLocalServer* server = nullptr;
    SlLocalConnection* accepted = nullptr;
    SlLocalListenOptions listen_options = {};
    SlLocalAcceptOptions accept_options = {};
    SlLocalIoOptions io_options = {};
    SlOwnedBytes bytes = {};
    ClientExchange exchange = {};
    std::string path = unique_pipe_path("io-timeout");
    HANDLE release_event = CreateEventW(NULL, TRUE, FALSE, NULL);

    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    if (release_event == NULL) {
        return 7;
    }
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        CloseHandle(release_event);
        return 1;
    }
    exchange.path = path;
    exchange.release_event = release_event;
    std::thread client(run_idle_client, &exchange);
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 2000U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr)
    {
        client.join();
        CloseHandle(release_event);
        sl_local_server_abort(server, nullptr);
        return 2;
    }
    {
        SlCancellationToken token = {};
        const unsigned char payload[] = {'x'};

        sl_cancellation_token_init(&token);
        if (expect_status(sl_cancellation_token_cancel(&token, SL_CANCELLATION_REASON_CANCELLED,
                                                       sl_str_from_cstr("test cancellation")),
                          SL_STATUS_OK) != 0)
        {
            client.join();
            CloseHandle(release_event);
            sl_local_connection_abort(accepted, nullptr);
            sl_local_server_abort(server, nullptr);
            return 3;
        }
        io_options = sl_local_io_options_default();
        io_options.cancellation = &token;
        if (expect_status(
                sl_local_connection_write_ex(
                    accepted, sl_bytes_from_parts(payload, sizeof(payload)), &io_options, nullptr),
                SL_STATUS_CANCELLED) != 0)
        {
            client.join();
            CloseHandle(release_event);
            sl_local_connection_abort(accepted, nullptr);
            sl_local_server_abort(server, nullptr);
            return 4;
        }
    }
    io_options = sl_local_io_options_default();
    io_options.has_timeout_ms = true;
    io_options.timeout_ms = 25U;
    if (expect_status(sl_local_connection_read_ex(accepted, &accepted_arena, 1U, &io_options,
                                                  &bytes, nullptr),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        bytes.length != 0U)
    {
        SetEvent(release_event);
        client.join();
        CloseHandle(release_event);
        sl_local_connection_abort(accepted, nullptr);
        sl_local_server_abort(server, nullptr);
        return 5;
    }
    SetEvent(release_event);
    client.join();
    CloseHandle(release_event);
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_abort(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        return 6;
    }
    return 0;
}

int test_read_until_binary_delimiter_and_limits()
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
    const unsigned char delimiter[] = {'\0', 'c'};
    const unsigned char overflow_delimiter[] = {'z'};
    const unsigned char expected[] = {'a', 'b', '\0', 'c'};
    ClientExchange exchange = {};
    std::string path = unique_pipe_path("read-until");

    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    exchange.path = path;
    std::thread client(run_split_client, &exchange);
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 2000U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr)
    {
        client.join();
        sl_local_server_abort(server, nullptr);
        return 2;
    }
    if (expect_status(sl_local_connection_read_until(
                          accepted, &accepted_arena,
                          sl_bytes_from_parts(delimiter, sizeof(delimiter)), 8U, &bytes, nullptr),
                      SL_STATUS_OK) != 0 ||
        bytes.length != sizeof(expected) || std::memcmp(bytes.ptr, expected, sizeof(expected)) != 0)
    {
        client.join();
        sl_local_connection_abort(accepted, nullptr);
        sl_local_server_abort(server, nullptr);
        return 3;
    }
    if (expect_status(sl_local_connection_read_until(accepted, &accepted_arena, sl_bytes_empty(),
                                                     8U, &bytes, nullptr),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_local_connection_read_until(
                          accepted, &accepted_arena,
                          sl_bytes_from_parts(overflow_delimiter, sizeof(overflow_delimiter)), 1U,
                          &bytes, nullptr),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        client.join();
        sl_local_connection_abort(accepted, nullptr);
        sl_local_server_abort(server, nullptr);
        return 4;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        return 5;
    }
    return 0;
}

} // namespace

int main()
{
    if (test_named_pipe_loopback_preserves_binary() != 0) {
        std::cerr << "test_named_pipe_loopback_preserves_binary failed\n";
        return 1;
    }
    if (test_connect_waits_for_late_server() != 0) {
        std::cerr << "test_connect_waits_for_late_server failed\n";
        return 1;
    }
    if (test_named_pipe_policy_and_unsupported_backend() != 0) {
        std::cerr << "test_named_pipe_policy_and_unsupported_backend failed\n";
        return 1;
    }
    if (test_accept_timeout_and_disposal() != 0) {
        std::cerr << "test_accept_timeout_and_disposal failed\n";
        return 1;
    }
    if (test_io_timeout_and_precancel() != 0) {
        std::cerr << "test_io_timeout_and_precancel failed\n";
        return 1;
    }
    if (test_read_until_binary_delimiter_and_limits() != 0) {
        std::cerr << "test_read_until_binary_delimiter_and_limits failed\n";
        return 1;
    }
    return 0;
}
