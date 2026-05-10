#include "sloppy/arena.h"
#include "sloppy/net.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

int expect_status(SlStatus status, SlStatusCode expected)
{
    return sl_status_code(status) == expected ? 0 : 1;
}

std::string unique_socket_path(const char* name)
{
    return std::string("/tmp/sloppy-local-ipc-") +
           std::to_string(static_cast<long long>(getpid())) + "-" + name + ".sock";
}

int create_stale_socket_node(const std::string& path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr = {};
    std::size_t length = path.size();

    if (fd < 0 || length >= sizeof(addr.sun_path)) {
        if (fd >= 0) {
            close(fd);
        }
        return 1;
    }
    addr.sun_family = AF_UNIX;
    for (std::size_t index = 0U; index < length; index += 1U) {
        addr.sun_path[index] = path[index];
    }
    if (bind(fd, reinterpret_cast<const sockaddr*>(&addr),
             static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + length + 1U)) != 0)
    {
        close(fd);
        return 2;
    }
    close(fd);
    return 0;
}

struct ClientExchange
{
    unsigned char storage[64U * 1024U] = {};
    SlArena arena = {};
    std::string path;
    std::mutex mutex;
    std::condition_variable close_ready;
    bool close_requested = false;
    int result = 0;
};

void request_idle_client_close(ClientExchange* exchange)
{
    if (exchange == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(exchange->mutex);
        exchange->close_requested = true;
    }
    exchange->close_ready.notify_all();
}

void run_idle_client(ClientExchange* exchange)
{
    SlLocalConnection* connection = nullptr;
    SlLocalConnectOptions options = {};

    if (exchange == nullptr) {
        return;
    }
    sl_arena_init(&exchange->arena, exchange->storage, sizeof(exchange->storage));
    options = sl_local_connect_options_default(sl_str_from_cstr(exchange->path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_connect(&exchange->arena, &options, &connection, nullptr),
                      SL_STATUS_OK) != 0 ||
        connection == nullptr)
    {
        exchange->result = 1;
        return;
    }
    {
        std::unique_lock<std::mutex> lock(exchange->mutex);
        exchange->close_ready.wait(lock, [exchange]() { return exchange->close_requested; });
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0) {
        exchange->result = 2;
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
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
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

#ifndef __APPLE__
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
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
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
#endif

int test_unix_socket_loopback_preserves_binary()
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
    std::string path = unique_socket_path("loopback");

    unlink(path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    listen_options.has_permissions = true;
    listen_options.permissions = 0600U;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0 ||
        server == nullptr || sl_local_server_state(server) != SL_LOCAL_SERVER_LISTENING)
    {
        unlink(path.c_str());
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
        unlink(path.c_str());
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
        unlink(path.c_str());
        return 3;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
        return 4;
    }
    unlink(path.c_str());
    return 0;
}

int test_stale_socket_cleanup_and_permissions()
{
    unsigned char server_storage[32U * 1024U] = {};
    SlArena server_arena = {};
    SlLocalServer* server = nullptr;
    SlLocalListenOptions options = {};
    std::string path = unique_socket_path("stale");
    std::string live_path = unique_socket_path("live");
    struct stat st = {};

    unlink(path.c_str());
    unlink(live_path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    options.has_permissions = true;
    options.permissions = 0600U;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
        return 1;
    }
    if (stat(path.c_str(), &st) != 0 || (st.st_mode & 0777U) != 0600U) {
        unlink(path.c_str());
        return 2;
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        access(path.c_str(), F_OK) == 0 || create_stale_socket_node(path) != 0)
    {
        unlink(path.c_str());
        return 3;
    }

    SlLocalServer* live = nullptr;
    options = sl_local_listen_options_default(sl_str_from_cstr(live_path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    options.unlink_existing = true;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &live, nullptr),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_local_server_abort(live, nullptr);
        unlink(path.c_str());
        unlink(live_path.c_str());
        return 4;
    }
    sl_local_server_close(live, nullptr);
    unlink(live_path.c_str());

    SlLocalServer* second = nullptr;
    options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    options.has_permissions = true;
    options.permissions = 0600U;
    options.unlink_existing = false;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &second, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        unlink(path.c_str());
        return 5;
    }
    options.unlink_existing = true;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &second, nullptr),
                      SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
        return 6;
    }
    sl_local_server_close(second, nullptr);
    unlink(path.c_str());
    return 0;
}

int test_accept_timeout_and_unsupported_backend()
{
    unsigned char server_storage[32U * 1024U] = {};
    unsigned char accepted_storage[32U * 1024U] = {};
    SlArena server_arena = {};
    SlArena accepted_arena = {};
    SlLocalServer* server = nullptr;
    SlLocalConnection* connection = nullptr;
    SlLocalConnection* accepted = nullptr;
    SlLocalConnectOptions connect_options = {};
    SlLocalListenOptions listen_options = {};
    SlLocalAcceptOptions accept_options = {};
    std::string path = unique_socket_path("timeout");

    unlink(path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 1;
    }
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
        return 2;
    }
    connect_options = sl_local_connect_options_default(sl_str_from_cstr(path.c_str()));
    connect_options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    connect_options.has_timeout_ms = true;
    connect_options.timeout_ms = 25U;
    if (expect_status(
            sl_local_endpoint_connect(&accepted_arena, &connect_options, &connection, nullptr),
            SL_STATUS_OK) != 0 ||
        connection == nullptr)
    {
        sl_local_server_abort(server, nullptr);
        unlink(path.c_str());
        return 3;
    }
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 1000U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr)
    {
        sl_local_connection_abort(connection, nullptr);
        sl_local_server_abort(server, nullptr);
        unlink(path.c_str());
        return 4;
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0)
    {
        sl_local_server_abort(server, nullptr);
        unlink(path.c_str());
        return 5;
    }
    connection = nullptr;
    accepted = nullptr;
    accept_options.timeout_ms = 25U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        accepted != nullptr)
    {
        sl_local_server_abort(server, nullptr);
        unlink(path.c_str());
        return 6;
    }
    {
        SlCancellationToken token = {};

        sl_cancellation_token_init(&token);
        if (expect_status(sl_cancellation_token_cancel(&token, SL_CANCELLATION_REASON_CANCELLED,
                                                       sl_str_from_cstr("test cancellation")),
                          SL_STATUS_OK) != 0)
        {
            sl_local_server_abort(server, nullptr);
            unlink(path.c_str());
            return 7;
        }
        accept_options = sl_local_accept_options_default();
        accept_options.cancellation = &token;
        if (expect_status(sl_local_server_accept(server, &accepted_arena, &accept_options,
                                                 &accepted, nullptr),
                          SL_STATUS_CANCELLED) != 0 ||
            accepted != nullptr)
        {
            sl_local_server_abort(server, nullptr);
            unlink(path.c_str());
            return 8;
        }
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_accept(server, &accepted_arena, nullptr, &accepted, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        unlink(path.c_str());
        return 9;
    }
    unlink(path.c_str());
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
    std::string path = unique_socket_path("io-timeout");

    unlink(path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
        return 1;
    }
    exchange.path = path;
    std::thread client(run_idle_client, &exchange);
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 2000U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr)
    {
        request_idle_client_close(&exchange);
        client.join();
        sl_local_server_abort(server, nullptr);
        unlink(path.c_str());
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
            request_idle_client_close(&exchange);
            client.join();
            sl_local_connection_abort(accepted, nullptr);
            sl_local_server_abort(server, nullptr);
            unlink(path.c_str());
            return 3;
        }
        io_options = sl_local_io_options_default();
        io_options.cancellation = &token;
        if (expect_status(
                sl_local_connection_write_ex(
                    accepted, sl_bytes_from_parts(payload, sizeof(payload)), &io_options, nullptr),
                SL_STATUS_CANCELLED) != 0)
        {
            request_idle_client_close(&exchange);
            client.join();
            sl_local_connection_abort(accepted, nullptr);
            sl_local_server_abort(server, nullptr);
            unlink(path.c_str());
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
        request_idle_client_close(&exchange);
        client.join();
        sl_local_connection_abort(accepted, nullptr);
        sl_local_server_abort(server, nullptr);
        unlink(path.c_str());
        return 5;
    }
    request_idle_client_close(&exchange);
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_abort(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
        return 6;
    }
    unlink(path.c_str());
    return 0;
}

int test_read_until_binary_delimiter_and_limits()
{
#ifdef __APPLE__
    return 0;
#else
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
    std::string path = unique_socket_path("read-until");

    unlink(path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
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
        unlink(path.c_str());
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
        unlink(path.c_str());
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
        unlink(path.c_str());
        return 4;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        unlink(path.c_str());
        return 5;
    }
    unlink(path.c_str());
    return 0;
#endif
}

} // namespace

int main()
{
    struct TestCase
    {
        const char* name;
        int (*fn)();
    };
    const TestCase tests[] = {
        {"test_unix_socket_loopback_preserves_binary", test_unix_socket_loopback_preserves_binary},
        {"test_stale_socket_cleanup_and_permissions", test_stale_socket_cleanup_and_permissions},
        {"test_accept_timeout_and_unsupported_backend",
         test_accept_timeout_and_unsupported_backend},
        {"test_io_timeout_and_precancel", test_io_timeout_and_precancel},
        {"test_read_until_binary_delimiter_and_limits",
         test_read_until_binary_delimiter_and_limits},
    };

    for (const TestCase& test : tests) {
        int result = test.fn();
        if (result != 0) {
            fprintf(stderr, "%s failed with code %d\n", test.name, result);
            return result;
        }
    }
    return 0;
}
