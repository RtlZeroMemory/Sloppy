#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

/*
 * src/platform/posix/net_local_posix.c
 *
 * POSIX Unix domain socket backend for Slop-owned LocalEndpoint resources. Public APIs
 * expose only SlLocalConnection/SlLocalServer pointers and JS-safe resource IDs above
 * this layer; raw file descriptors and sockaddr_un details stay private here.
 */
#include "sloppy/net.h"

#include "sloppy/builder.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY 8192U

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct SlLocalConnection
{
    int fd;
    SlLocalConnectionState state;
    unsigned char* read_buffer;
    size_t read_buffer_capacity;
};

struct SlLocalServer
{
    int fd;
    SlLocalServerState state;
    char path[sizeof(((struct sockaddr_un*)0)->sun_path)];
    size_t read_buffer_capacity;
};

static SlStr sl_local_literal(const char* text)
{
    return sl_str_from_cstr(text);
}

static void sl_local_fail_diag(SlDiag* out_diag, SlDiagCode code, SlStr message)
{
    if (out_diag == NULL) {
        return;
    }
    *out_diag = (SlDiag){0};
    out_diag->severity = SL_DIAG_SEVERITY_ERROR;
    out_diag->code = code;
    out_diag->message = message;
    out_diag->primary_span = sl_source_span_unknown();
}

static SlStatus sl_local_fail(SlDiag* out_diag, SlDiagCode code, SlStatusCode status, SlStr message)
{
    sl_local_fail_diag(out_diag, code, message);
    return sl_status_from_code(status);
}

static SlStatus sl_local_errno_status(int error, SlDiag* out_diag, SlDiagCode code,
                                      SlStatusCode fallback, SlStr message)
{
    if (error == EEXIST || error == EADDRINUSE) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS,
                             SL_STATUS_INVALID_STATE,
                             sl_local_literal("local IPC endpoint already exists"));
    }
    if (error == ENOENT || error == ENOTDIR || error == EACCES || error == EPERM) {
        return sl_local_fail(out_diag, code, fallback, message);
    }
    if (error == ENOMEM) {
        return sl_local_fail(out_diag, code, SL_STATUS_OUT_OF_MEMORY, message);
    }
    return sl_local_fail(out_diag, code, fallback, message);
}

static bool sl_local_backend_supported(SlLocalEndpointBackend backend)
{
    return backend == SL_LOCAL_ENDPOINT_BACKEND_AUTO || backend == SL_LOCAL_ENDPOINT_BACKEND_UNIX;
}

static SlStatus sl_local_copy_path(SlStr path, char out[sizeof(((struct sockaddr_un*)0)->sun_path)],
                                   size_t* out_length, SlDiag* out_diag)
{
    size_t max_len = sizeof(((struct sockaddr_un*)0)->sun_path);

    if (out == NULL || out_length == NULL || path.ptr == NULL || path.length == 0U ||
        path.length >= max_len)
    {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_INVALID_PATH,
                             SL_STATUS_INVALID_ARGUMENT,
                             sl_local_literal("local IPC endpoint path is invalid"));
    }
    for (size_t index = 0U; index < path.length; index += 1U) {
        if (path.ptr[index] == '\0') {
            return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_INVALID_PATH,
                                 SL_STATUS_INVALID_ARGUMENT,
                                 sl_local_literal("local IPC endpoint path is invalid"));
        }
        out[index] = path.ptr[index];
    }
    out[path.length] = '\0';
    *out_length = path.length;
    return sl_status_ok();
}

static void sl_local_make_addr(const char* path, size_t path_length, struct sockaddr_un* out,
                               socklen_t* out_len)
{
    *out = (struct sockaddr_un){0};
    out->sun_family = AF_UNIX;
    for (size_t index = 0U; index < path_length; index += 1U) {
        out->sun_path[index] = path[index];
    }
    out->sun_path[path_length] = '\0';
    *out_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_length + 1U);
}

static SlStatus sl_local_alloc_connection(SlArena* arena, size_t read_capacity,
                                          SlLocalConnection** out)
{
    void* memory = NULL;
    void* read_memory = NULL;
    size_t capacity = read_capacity == 0U ? SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY : read_capacity;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, sizeof(SlLocalConnection), _Alignof(SlLocalConnection), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, capacity, _Alignof(unsigned char), &read_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (SlLocalConnection*)memory;
    **out = (SlLocalConnection){0};
    (*out)->fd = -1;
    (*out)->state = SL_LOCAL_CONNECTION_CONNECTING;
    (*out)->read_buffer = (unsigned char*)read_memory;
    (*out)->read_buffer_capacity = capacity;
    return sl_status_ok();
}

static SlStatus sl_local_alloc_server(SlArena* arena, size_t read_capacity, SlLocalServer** out)
{
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, sizeof(SlLocalServer), _Alignof(SlLocalServer), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (SlLocalServer*)memory;
    **out = (SlLocalServer){0};
    (*out)->fd = -1;
    (*out)->state = SL_LOCAL_SERVER_UNBOUND;
    (*out)->read_buffer_capacity =
        read_capacity == 0U ? SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY : read_capacity;
    return sl_status_ok();
}

static SlStatus sl_local_configure_fd(int fd, SlDiag* out_diag)
{
    int flags = 0;

    if (fd < 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) {
        return sl_local_errno_status(errno, out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                                     SL_STATUS_INTERNAL,
                                     sl_local_literal("local IPC backend is unavailable"));
    }
#ifdef SO_NOSIGPIPE
    {
        int enabled = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
            return sl_local_errno_status(errno, out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                                         SL_STATUS_INTERNAL,
                                         sl_local_literal("local IPC backend is unavailable"));
        }
    }
#endif
    return sl_status_ok();
}

static SlStatus sl_local_probe_socket_stale(const char* path, size_t path_length, bool* out_stale,
                                            SlDiag* out_diag)
{
    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    SlStatus status;
    int fd = -1;

    if (path == NULL || out_stale == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_stale = false;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return sl_local_errno_status(errno, out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                                     SL_STATUS_INTERNAL,
                                     sl_local_literal("local IPC backend is unavailable"));
    }
    status = sl_local_configure_fd(fd, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)close(fd);
        return status;
    }
    sl_local_make_addr(path, path_length, &addr, &addr_len);
    if (connect(fd, (const struct sockaddr*)&addr, addr_len) == 0) {
        (void)close(fd);
        return sl_status_ok();
    }
    switch (errno) {
    case ECONNREFUSED:
    case ENOENT:
        *out_stale = true;
        (void)close(fd);
        return sl_status_ok();
    default:
        break;
    }
    status = sl_local_errno_status(errno, out_diag, SL_DIAG_NET_LOCAL_IPC_PATH_DENIED,
                                   SL_STATUS_INVALID_STATE,
                                   sl_local_literal("local IPC path was denied by policy"));
    (void)close(fd);
    return status;
}

static SlStatus sl_local_wait_fd(int fd, bool write_ready, uint32_t timeout_ms, bool has_timeout,
                                 SlDiag* out_diag, SlDiagCode timeout_code, SlStr timeout_message)
{
    struct pollfd pfd = {0};
    int timeout = -1;
    int result = 0;

    if (fd < 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    pfd.fd = fd;
    pfd.events = write_ready ? POLLOUT : POLLIN;
    if (has_timeout) {
        timeout = timeout_ms > (uint32_t)INT32_MAX ? INT32_MAX : (int)timeout_ms;
    }
    do {
        result = poll(&pfd, 1U, timeout);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        return sl_local_fail(out_diag, timeout_code, SL_STATUS_DEADLINE_EXCEEDED, timeout_message);
    }
    if (result < 0) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                             SL_STATUS_INTERNAL,
                             sl_local_literal("local IPC backend is unavailable"));
    }
    return sl_status_ok();
}

SlLocalConnectOptions sl_local_connect_options_default(SlStr path)
{
    SlLocalConnectOptions options = {0};

    options.path = path;
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_AUTO;
    options.read_buffer_capacity = SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY;
    return options;
}

SlLocalListenOptions sl_local_listen_options_default(SlStr path)
{
    SlLocalListenOptions options = {0};

    options.path = path;
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_AUTO;
    options.backlog = 128U;
    options.read_buffer_capacity = SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY;
    return options;
}

SlLocalAcceptOptions sl_local_accept_options_default(void)
{
    SlLocalAcceptOptions options = {0};
    return options;
}

SlStatus sl_local_endpoint_connect(SlArena* arena, const SlLocalConnectOptions* options,
                                   SlLocalConnection** out_connection, SlDiag* out_diag)
{
    SlLocalConnection* connection = NULL;
    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    char path[sizeof(((struct sockaddr_un*)0)->sun_path)];
    size_t path_length = 0U;
    SlStatus status;
    int fd = -1;

    if (out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    if (arena == NULL || options == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_local_backend_supported(options->backend)) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM,
                             SL_STATUS_UNSUPPORTED,
                             sl_local_literal("local IPC platform is unsupported"));
    }
    status = sl_local_copy_path(options->path, path, &path_length, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (options->has_timeout_ms) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED, SL_STATUS_UNSUPPORTED,
                             sl_local_literal("local IPC connect timeout is unsupported"));
    }
    status = sl_local_alloc_connection(arena, options->read_buffer_capacity, &connection);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        connection->state = SL_LOCAL_CONNECTION_FAILED;
        return sl_local_errno_status(errno, out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                                     SL_STATUS_INTERNAL,
                                     sl_local_literal("local IPC backend is unavailable"));
    }
    status = sl_local_configure_fd(fd, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)close(fd);
        connection->state = SL_LOCAL_CONNECTION_FAILED;
        return status;
    }
    sl_local_make_addr(path, path_length, &addr, &addr_len);
    if (connect(fd, (const struct sockaddr*)&addr, addr_len) != 0) {
        int error = errno;
        (void)close(fd);
        connection->state = SL_LOCAL_CONNECTION_FAILED;
        return sl_local_errno_status(error, out_diag, SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED,
                                     SL_STATUS_INVALID_STATE,
                                     sl_local_literal("local IPC connect failed"));
    }
    connection->fd = fd;
    connection->state = SL_LOCAL_CONNECTION_CONNECTED;
    *out_connection = connection;
    return sl_status_ok();
}

SlStatus sl_local_endpoint_listen(SlArena* arena, const SlLocalListenOptions* options,
                                  SlLocalServer** out_server, SlDiag* out_diag)
{
    SlLocalServer* server = NULL;
    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    char path[sizeof(((struct sockaddr_un*)0)->sun_path)];
    size_t path_length = 0U;
    struct stat st;
    SlStatus status;
    int fd = -1;
    int backlog = 0;
    bool is_stale = false;

    if (out_server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_server = NULL;
    if (arena == NULL || options == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_local_backend_supported(options->backend)) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM,
                             SL_STATUS_UNSUPPORTED,
                             sl_local_literal("local IPC platform is unsupported"));
    }
    status = sl_local_copy_path(options->path, path, &path_length, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode) || !options->unlink_existing) {
            return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS,
                                 SL_STATUS_INVALID_STATE,
                                 sl_local_literal("local IPC endpoint already exists"));
        }
        status = sl_local_probe_socket_stale(path, path_length, &is_stale, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (!is_stale) {
            return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS,
                                 SL_STATUS_INVALID_STATE,
                                 sl_local_literal("local IPC endpoint already exists"));
        }
        if (unlink(path) != 0) {
            if (errno != ENOENT) {
                return sl_local_errno_status(
                    errno, out_diag, SL_DIAG_NET_LOCAL_IPC_STALE_CLEANUP_FAILED,
                    SL_STATUS_INVALID_STATE,
                    sl_local_literal("stale local IPC endpoint cleanup failed"));
            }
        }
    }
    else if (errno != ENOENT) {
        return sl_local_errno_status(errno, out_diag, SL_DIAG_NET_LOCAL_IPC_PATH_DENIED,
                                     SL_STATUS_INVALID_STATE,
                                     sl_local_literal("local IPC path was denied by policy"));
    }
    status = sl_local_alloc_server(arena, options->read_buffer_capacity, &server);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        server->state = SL_LOCAL_SERVER_FAILED;
        return sl_local_errno_status(errno, out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                                     SL_STATUS_INTERNAL,
                                     sl_local_literal("local IPC backend is unavailable"));
    }
    status = sl_local_configure_fd(fd, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)close(fd);
        server->state = SL_LOCAL_SERVER_FAILED;
        return status;
    }
    sl_local_make_addr(path, path_length, &addr, &addr_len);
    if (bind(fd, (const struct sockaddr*)&addr, addr_len) != 0) {
        int error = errno;
        (void)close(fd);
        server->state = SL_LOCAL_SERVER_FAILED;
        return sl_local_errno_status(error, out_diag, SL_DIAG_NET_LOCAL_IPC_LISTEN_FAILED,
                                     SL_STATUS_INVALID_STATE,
                                     sl_local_literal("local IPC listen failed"));
    }
    if (options->has_permissions && chmod(path, (mode_t)options->permissions) != 0) {
        int error = errno;
        (void)close(fd);
        (void)unlink(path);
        server->state = SL_LOCAL_SERVER_FAILED;
        return sl_local_errno_status(error, out_diag, SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED,
                                     SL_STATUS_UNSUPPORTED,
                                     sl_local_literal("local IPC permission mode is unsupported"));
    }
    backlog = options->backlog == 0U ? 128 : (int)options->backlog;
    if (listen(fd, backlog) != 0) {
        int error = errno;
        (void)close(fd);
        (void)unlink(path);
        server->state = SL_LOCAL_SERVER_FAILED;
        return sl_local_errno_status(error, out_diag, SL_DIAG_NET_LOCAL_IPC_LISTEN_FAILED,
                                     SL_STATUS_INVALID_STATE,
                                     sl_local_literal("local IPC listen failed"));
    }
    server->fd = fd;
    server->state = SL_LOCAL_SERVER_LISTENING;
    for (size_t index = 0U; index <= path_length; index += 1U) {
        server->path[index] = path[index];
    }
    *out_server = server;
    return sl_status_ok();
}

SlLocalServerState sl_local_server_state(const SlLocalServer* server)
{
    return server == NULL ? SL_LOCAL_SERVER_FAILED : server->state;
}

SlStatus sl_local_server_accept(SlLocalServer* server, SlArena* arena,
                                const SlLocalAcceptOptions* options,
                                SlLocalConnection** out_connection, SlDiag* out_diag)
{
    SlLocalConnection* connection = NULL;
    SlStatus status;
    int fd = -1;

    if (server == NULL || arena == NULL || out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    if (server->state != SL_LOCAL_SERVER_LISTENING) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_DISPOSED, SL_STATUS_INVALID_STATE,
                             sl_local_literal("local IPC connection or server is disposed"));
    }
    if (options != NULL && options->has_timeout_ms) {
        status = sl_local_wait_fd(server->fd, false, options->timeout_ms, true, out_diag,
                                  SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED,
                                  sl_local_literal("local IPC accept was cancelled or timed out"));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_local_alloc_connection(arena, server->read_buffer_capacity, &connection);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    do {
        fd = accept(server->fd, NULL, NULL);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        connection->state = SL_LOCAL_CONNECTION_FAILED;
        return sl_local_errno_status(
            errno, out_diag, SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED, SL_STATUS_INVALID_STATE,
            sl_local_literal("local IPC accept was cancelled or timed out"));
    }
    status = sl_local_configure_fd(fd, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)close(fd);
        connection->state = SL_LOCAL_CONNECTION_FAILED;
        return status;
    }
    connection->fd = fd;
    connection->state = SL_LOCAL_CONNECTION_CONNECTED;
    *out_connection = connection;
    return sl_status_ok();
}

static SlStatus sl_local_server_finish_close(SlLocalServer* server, SlLocalServerState state,
                                             SlDiag* out_diag)
{
    int unlink_error = 0;

    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_LOCAL_SERVER_CLOSED || server->state == SL_LOCAL_SERVER_ABORTED) {
        return sl_status_ok();
    }
    server->state = SL_LOCAL_SERVER_CLOSING;
    if (server->fd >= 0) {
        (void)close(server->fd);
        server->fd = -1;
    }
    if (server->path[0] != '\0' && unlink(server->path) != 0 && errno != ENOENT) {
        unlink_error = errno;
    }
    server->state = state;
    if (unlink_error != 0) {
        return sl_local_errno_status(
            unlink_error, out_diag, SL_DIAG_NET_LOCAL_IPC_STALE_CLEANUP_FAILED,
            SL_STATUS_INVALID_STATE, sl_local_literal("stale local IPC endpoint cleanup failed"));
    }
    server->path[0] = '\0';
    return sl_status_ok();
}

SlStatus sl_local_server_close(SlLocalServer* server, SlDiag* out_diag)
{
    return sl_local_server_finish_close(server, SL_LOCAL_SERVER_CLOSED, out_diag);
}

SlStatus sl_local_server_abort(SlLocalServer* server, SlDiag* out_diag)
{
    return sl_local_server_finish_close(server, SL_LOCAL_SERVER_ABORTED, out_diag);
}

SlLocalConnectionState sl_local_connection_state(const SlLocalConnection* connection)
{
    return connection == NULL ? SL_LOCAL_CONNECTION_FAILED : connection->state;
}

static SlStatus sl_local_require_connected(SlLocalConnection* connection, SlDiag* out_diag)
{
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_LOCAL_CONNECTION_CONNECTED || connection->fd < 0) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_DISPOSED, SL_STATUS_INVALID_STATE,
                             sl_local_literal("local IPC connection or server is disposed"));
    }
    return sl_status_ok();
}

SlStatus sl_local_connection_write(SlLocalConnection* connection, SlBytes bytes, SlDiag* out_diag)
{
    SlStatus status = sl_local_require_connected(connection, out_diag);
    size_t written = 0U;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (bytes.length != 0U && bytes.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    while (written < bytes.length) {
        ssize_t result =
            send(connection->fd, bytes.ptr + written, bytes.length - written, MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return sl_local_errno_status(
                errno, out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                SL_STATUS_INVALID_STATE,
                sl_local_literal("local IPC read or write was cancelled or timed out"));
        }
        written += (size_t)result;
    }
    return sl_status_ok();
}

SlStatus sl_local_connection_write_text(SlLocalConnection* connection, SlStr text, SlDiag* out_diag)
{
    if (text.length != 0U && text.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_local_connection_write(
        connection, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length), out_diag);
}

SlStatus sl_local_connection_read(SlLocalConnection* connection, SlArena* arena, size_t max_bytes,
                                  SlOwnedBytes* out, SlDiag* out_diag)
{
    SlStatus status = sl_local_require_connected(connection, out_diag);
    size_t capacity = 0U;
    ssize_t result = 0;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    capacity = max_bytes == 0U || max_bytes > connection->read_buffer_capacity
                   ? connection->read_buffer_capacity
                   : max_bytes;
    do {
        result = recv(connection->fd, connection->read_buffer, capacity, 0);
    } while (result < 0 && errno == EINTR);
    if (result <= 0) {
        if (result == 0) {
            connection->state = SL_LOCAL_CONNECTION_CLOSED;
            return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_DISPOSED, SL_STATUS_INVALID_STATE,
                                 sl_local_literal("local IPC connection or server is disposed"));
        }
        return sl_local_errno_status(
            errno, out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED, SL_STATUS_INVALID_STATE,
            sl_local_literal("local IPC read or write was cancelled or timed out"));
    }
    return sl_bytes_copy_to_arena(
        arena, sl_bytes_from_parts(connection->read_buffer, (size_t)result), out);
}

SlStatus sl_local_connection_read_until(SlLocalConnection* connection, SlArena* arena,
                                        SlBytes delimiter, size_t max_bytes, SlOwnedBytes* out,
                                        SlDiag* out_diag)
{
    SlByteBuilder builder;
    SlStatus status;
    size_t limit = 0U;

    if (connection == NULL || arena == NULL || out == NULL || delimiter.ptr == NULL ||
        delimiter.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    limit = max_bytes == 0U ? connection->read_buffer_capacity : max_bytes;
    status = sl_byte_builder_init_arena(&builder, arena, 0U, limit);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    while (sl_byte_builder_length(&builder) < limit) {
        SlOwnedBytes chunk = {0};
        SlBytes view;
        status = sl_local_connection_read(connection, arena, 1U, &chunk, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_byte_builder_append_bytes(&builder, sl_owned_bytes_as_view(chunk));
        if (!sl_status_is_ok(status)) {
            return sl_local_fail(
                out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED, SL_STATUS_CAPACITY_EXCEEDED,
                sl_local_literal("local IPC read or write was cancelled or timed out"));
        }
        view = sl_byte_builder_view(&builder);
        if (sl_bytes_ends_with(view, delimiter)) {
            return sl_bytes_copy_to_arena(arena, view, out);
        }
    }
    return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                         SL_STATUS_CAPACITY_EXCEEDED,
                         sl_local_literal("local IPC read or write was cancelled or timed out"));
}

SlStatus sl_local_connection_read_line(SlLocalConnection* connection, SlArena* arena,
                                       size_t max_bytes, SlOwnedStr* out, SlDiag* out_diag)
{
    static const unsigned char newline = '\n';
    SlOwnedBytes bytes = {0};
    SlStatus status;
    size_t length = 0U;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_local_connection_read_until(connection, arena, sl_bytes_from_parts(&newline, 1U),
                                            max_bytes, &bytes, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    length = bytes.length;
    if (length != 0U && bytes.ptr[length - 1U] == '\n') {
        length -= 1U;
    }
    if (length != 0U && bytes.ptr[length - 1U] == '\r') {
        length -= 1U;
    }
    return sl_str_copy_to_arena(arena, sl_str_from_parts((const char*)bytes.ptr, length), out);
}

static SlStatus sl_local_connection_finish_close(SlLocalConnection* connection,
                                                 SlLocalConnectionState state)
{
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state == SL_LOCAL_CONNECTION_CLOSED ||
        connection->state == SL_LOCAL_CONNECTION_ABORTED)
    {
        return sl_status_ok();
    }
    connection->state = SL_LOCAL_CONNECTION_CLOSING;
    if (connection->fd >= 0) {
        (void)close(connection->fd);
        connection->fd = -1;
    }
    connection->state = state;
    return sl_status_ok();
}

SlStatus sl_local_connection_close(SlLocalConnection* connection, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_local_connection_finish_close(connection, SL_LOCAL_CONNECTION_CLOSED);
}

SlStatus sl_local_connection_abort(SlLocalConnection* connection, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_local_connection_finish_close(connection, SL_LOCAL_CONNECTION_ABORTED);
}
