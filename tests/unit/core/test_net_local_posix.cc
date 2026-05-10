#include "sloppy/arena.h"
#include "sloppy/net.h"

#include <cstddef>
#include <cstring>
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
            (void)close(fd);
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
        (void)close(fd);
        return 2;
    }
    (void)close(fd);
    return 0;
}

struct ClientExchange
{
    unsigned char storage[64U * 1024U] = {};
    SlArena arena = {};
    std::string path;
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
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_connect(&exchange->arena, &options, &connection, nullptr),
                      SL_STATUS_OK) != 0 ||
        connection == nullptr)
    {
        exchange->result = 1;
        return;
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
        (void)sl_local_connection_abort(connection, nullptr);
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
        (void)sl_local_connection_abort(connection, nullptr);
        return;
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0) {
        exchange->result = 3;
    }
}

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

    (void)unlink(path.c_str());
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
        (void)unlink(path.c_str());
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
        (void)unlink(path.c_str());
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
        (void)unlink(path.c_str());
        return 3;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        (void)unlink(path.c_str());
        return 4;
    }
    (void)unlink(path.c_str());
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

    (void)unlink(path.c_str());
    (void)unlink(live_path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    options.has_permissions = true;
    options.permissions = 0600U;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        (void)unlink(path.c_str());
        return 1;
    }
    if (stat(path.c_str(), &st) != 0 || (st.st_mode & 0777U) != 0600U) {
        (void)unlink(path.c_str());
        return 2;
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        access(path.c_str(), F_OK) == 0 || create_stale_socket_node(path) != 0)
    {
        (void)unlink(path.c_str());
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
        (void)sl_local_server_abort(live, nullptr);
        (void)unlink(path.c_str());
        (void)unlink(live_path.c_str());
        return 4;
    }
    (void)sl_local_server_close(live, nullptr);
    (void)unlink(live_path.c_str());

    SlLocalServer* second = nullptr;
    options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    options.has_permissions = true;
    options.permissions = 0600U;
    options.unlink_existing = false;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &second, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        (void)unlink(path.c_str());
        return 5;
    }
    options.unlink_existing = true;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &options, &second, nullptr),
                      SL_STATUS_OK) != 0)
    {
        (void)unlink(path.c_str());
        return 6;
    }
    (void)sl_local_server_close(second, nullptr);
    (void)unlink(path.c_str());
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

    (void)unlink(path.c_str());
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
        (void)unlink(path.c_str());
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
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
        return 3;
    }
    accept_options.has_timeout_ms = true;
    accept_options.timeout_ms = 1000U;
    if (expect_status(
            sl_local_server_accept(server, &accepted_arena, &accept_options, &accepted, nullptr),
            SL_STATUS_OK) != 0 ||
        accepted == nullptr)
    {
        (void)sl_local_connection_abort(connection, nullptr);
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
        return 4;
    }
    if (expect_status(sl_local_connection_close(connection, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0)
    {
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
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
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
        return 6;
    }
    {
        SlCancellationToken token = {};

        sl_cancellation_token_init(&token);
        if (expect_status(sl_cancellation_token_cancel(&token, SL_CANCELLATION_REASON_CANCELLED,
                                                       sl_str_from_cstr("test cancellation")),
                          SL_STATUS_OK) != 0)
        {
            (void)sl_local_server_abort(server, nullptr);
            (void)unlink(path.c_str());
            return 7;
        }
        accept_options = sl_local_accept_options_default();
        accept_options.cancellation = &token;
        if (expect_status(sl_local_server_accept(server, &accepted_arena, &accept_options,
                                                 &accepted, nullptr),
                          SL_STATUS_CANCELLED) != 0 ||
            accepted != nullptr)
        {
            (void)sl_local_server_abort(server, nullptr);
            (void)unlink(path.c_str());
            return 8;
        }
    }
    if (expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_accept(server, &accepted_arena, nullptr, &accepted, nullptr),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        (void)unlink(path.c_str());
        return 9;
    }
    (void)unlink(path.c_str());
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

    (void)unlink(path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        (void)unlink(path.c_str());
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
        client.join();
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
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
            (void)sl_local_connection_abort(accepted, nullptr);
            (void)sl_local_server_abort(server, nullptr);
            (void)unlink(path.c_str());
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
            (void)sl_local_connection_abort(accepted, nullptr);
            (void)sl_local_server_abort(server, nullptr);
            (void)unlink(path.c_str());
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
        client.join();
        (void)sl_local_connection_abort(accepted, nullptr);
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
        return 5;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_abort(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        (void)unlink(path.c_str());
        return 6;
    }
    (void)unlink(path.c_str());
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
    std::string path = unique_socket_path("read-until");

    (void)unlink(path.c_str());
    sl_arena_init(&server_arena, server_storage, sizeof(server_storage));
    sl_arena_init(&accepted_arena, accepted_storage, sizeof(accepted_storage));
    listen_options = sl_local_listen_options_default(sl_str_from_cstr(path.c_str()));
    listen_options.backend = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
    if (expect_status(sl_local_endpoint_listen(&server_arena, &listen_options, &server, nullptr),
                      SL_STATUS_OK) != 0)
    {
        (void)unlink(path.c_str());
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
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
        return 2;
    }
    if (expect_status(sl_local_connection_read_until(
                          accepted, &accepted_arena,
                          sl_bytes_from_parts(delimiter, sizeof(delimiter)), 8U, &bytes, nullptr),
                      SL_STATUS_OK) != 0 ||
        bytes.length != sizeof(expected) || std::memcmp(bytes.ptr, expected, sizeof(expected)) != 0)
    {
        client.join();
        (void)sl_local_connection_abort(accepted, nullptr);
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
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
        (void)sl_local_connection_abort(accepted, nullptr);
        (void)sl_local_server_abort(server, nullptr);
        (void)unlink(path.c_str());
        return 4;
    }
    client.join();
    if (exchange.result != 0 ||
        expect_status(sl_local_connection_close(accepted, nullptr), SL_STATUS_OK) != 0 ||
        expect_status(sl_local_server_close(server, nullptr), SL_STATUS_OK) != 0)
    {
        (void)unlink(path.c_str());
        return 5;
    }
    (void)unlink(path.c_str());
    return 0;
}

} // namespace

int main()
{
    if (test_unix_socket_loopback_preserves_binary() != 0) {
        return 1;
    }
    if (test_stale_socket_cleanup_and_permissions() != 0) {
        return 1;
    }
    if (test_accept_timeout_and_unsupported_backend() != 0) {
        return 1;
    }
    if (test_io_timeout_and_precancel() != 0) {
        return 1;
    }
    if (test_read_until_binary_delimiter_and_limits() != 0) {
        return 1;
    }
    return 0;
}
