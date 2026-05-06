/*
 * src/platform/libuv/net_tcp_libuv.c
 *
 * Libuv-backed TCP client/connection runtime. Public headers expose only Slop-owned
 * connection/resource types; libuv handles and sockaddr storage stay private here.
 */
#include "sloppy/net.h"

#include "sloppy/builder.h"

#include <string.h>
#include <uv.h>

#define SL_TCP_DEFAULT_READ_BUFFER_CAPACITY 8192U
#define SL_TCP_MAX_HOST_BYTES 255U

typedef struct SlTcpIoState
{
    bool done;
    int status;
    bool timeout;
    bool closed;
    const unsigned char* write_data;
    size_t write_length;
    unsigned char* read_data;
    size_t read_capacity;
    size_t read_length;
} SlTcpIoState;

struct SlTcpConnection
{
    uv_loop_t loop;
    uv_tcp_t handle;
    uv_connect_t connect_req;
    uv_write_t write_req;
    uv_timer_t timer;
    bool loop_initialized;
    bool handle_initialized;
    bool timer_initialized;
    bool close_started;
    bool close_done;
    SlTcpConnectionState state;
    unsigned char* read_buffer;
    size_t read_buffer_capacity;
    char local_host[INET6_ADDRSTRLEN];
    char remote_host[INET6_ADDRSTRLEN];
    uint16_t local_port;
    uint16_t remote_port;
    SlNetworkAddressFamily local_family;
    SlNetworkAddressFamily remote_family;
    SlTcpIoState io;
};

static SlStr sl_tcp_literal(const char* text)
{
    return sl_str_from_cstr(text);
}

static void sl_tcp_fail_diag(SlDiag* out_diag, SlDiagCode code, SlStr message)
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

static SlStatus sl_tcp_fail(SlDiag* out_diag, SlDiagCode code, SlStatusCode status, SlStr message)
{
    sl_tcp_fail_diag(out_diag, code, message);
    return sl_status_from_code(status);
}

static SlStatus sl_tcp_status_from_uv(int uv_status, SlDiag* out_diag, SlDiagCode code,
                                      SlStr message)
{
    if (uv_status == 0) {
        return sl_status_ok();
    }
    if (uv_status == UV_ENOMEM) {
        return sl_tcp_fail(out_diag, code, SL_STATUS_OUT_OF_MEMORY, message);
    }
    if (uv_status == UV_ETIMEDOUT) {
        return sl_tcp_fail(out_diag, SL_DIAG_NET_CONNECT_TIMEOUT, SL_STATUS_DEADLINE_EXCEEDED,
                           sl_tcp_literal("TCP connect timed out"));
    }
    if (uv_status == UV_ECANCELED) {
        return sl_tcp_fail(out_diag, SL_DIAG_NET_CONNECT_CANCELLED, SL_STATUS_CANCELLED,
                           sl_tcp_literal("TCP connect was cancelled"));
    }
    return sl_tcp_fail(out_diag, code, SL_STATUS_INTERNAL, message);
}

static bool sl_tcp_copy_host_to_cstr(SlStr host, char out[SL_TCP_MAX_HOST_BYTES + 1U])
{
    if (out == NULL || host.ptr == NULL || host.length == 0U || host.length > SL_TCP_MAX_HOST_BYTES)
    {
        return false;
    }
    memcpy(out, host.ptr, host.length);
    out[host.length] = '\0';
    return true;
}

static void sl_tcp_close_cb(uv_handle_t* handle)
{
    SlTcpConnection* connection = handle == NULL ? NULL : (SlTcpConnection*)handle->data;
    if (connection != NULL) {
        connection->close_done = true;
        connection->io.closed = true;
    }
}

static void sl_tcp_timer_close_cb(uv_handle_t* handle)
{
    SlTcpConnection* connection = handle == NULL ? NULL : (SlTcpConnection*)handle->data;
    if (connection != NULL) {
        connection->timer_initialized = false;
    }
}

static void sl_tcp_timer_cb(uv_timer_t* timer)
{
    SlTcpConnection* connection = timer == NULL ? NULL : (SlTcpConnection*)timer->data;
    if (connection == NULL) {
        return;
    }
    connection->io.timeout = true;
    connection->io.done = true;
    connection->io.status = UV_ETIMEDOUT;
    if (connection->handle_initialized && !uv_is_closing((uv_handle_t*)&connection->handle)) {
        uv_close((uv_handle_t*)&connection->handle, sl_tcp_close_cb);
        connection->close_started = true;
        connection->state = SL_TCP_CONNECTION_FAILED;
    }
}

static void sl_tcp_connect_cb(uv_connect_t* req, int status)
{
    SlTcpConnection* connection = req == NULL ? NULL : (SlTcpConnection*)req->data;
    if (connection == NULL) {
        return;
    }
    if (connection->io.done && connection->io.timeout) {
        return;
    }
    connection->io.status = status;
    connection->io.done = true;
}

static void sl_tcp_write_cb(uv_write_t* req, int status)
{
    SlTcpConnection* connection = req == NULL ? NULL : (SlTcpConnection*)req->data;
    if (connection == NULL) {
        return;
    }
    connection->io.status = status;
    connection->io.done = true;
}

static void sl_tcp_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    SlTcpConnection* connection = handle == NULL ? NULL : (SlTcpConnection*)handle->data;
    size_t capacity = 0U;

    (void)suggested_size;
    if (buf == NULL) {
        return;
    }
    if (connection == NULL || connection->io.read_data == NULL ||
        connection->io.read_capacity == 0U)
    {
        *buf = uv_buf_init(NULL, 0U);
        return;
    }
    capacity = connection->io.read_capacity > UINT_MAX ? UINT_MAX : connection->io.read_capacity;
    *buf = uv_buf_init((char*)connection->io.read_data, (unsigned int)capacity);
}

static void sl_tcp_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    SlTcpConnection* connection = stream == NULL ? NULL : (SlTcpConnection*)stream->data;

    (void)buf;
    if (connection == NULL) {
        return;
    }
    if (nread > 0) {
        connection->io.read_length = (size_t)nread;
        connection->io.status = 0;
        connection->io.done = true;
        (void)uv_read_stop(stream);
        return;
    }
    if (nread < 0) {
        connection->io.read_length = 0U;
        connection->io.status = (int)nread;
        connection->io.done = true;
        (void)uv_read_stop(stream);
    }
}

static SlNetworkAddressFamily sl_tcp_endpoint_from_sockaddr(const struct sockaddr* addr,
                                                            char host[INET6_ADDRSTRLEN],
                                                            uint16_t* out_port)
{
    int port = 0;

    if (host != NULL) {
        host[0] = '\0';
    }
    if (out_port != NULL) {
        *out_port = 0U;
    }
    if (addr == NULL || host == NULL || out_port == NULL) {
        return SL_NETWORK_ADDRESS_UNSPECIFIED;
    }

    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
        if (uv_ip4_name(in, host, INET6_ADDRSTRLEN) != 0) {
            return SL_NETWORK_ADDRESS_UNSPECIFIED;
        }
        port = ntohs(in->sin_port);
        *out_port = (uint16_t)port;
        return SL_NETWORK_ADDRESS_IPV4;
    }
    if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)addr;
        if (uv_ip6_name(in6, host, INET6_ADDRSTRLEN) != 0) {
            return SL_NETWORK_ADDRESS_UNSPECIFIED;
        }
        port = ntohs(in6->sin6_port);
        *out_port = (uint16_t)port;
        return SL_NETWORK_ADDRESS_IPV6;
    }
    return SL_NETWORK_ADDRESS_UNSPECIFIED;
}

static void sl_tcp_refresh_endpoints(SlTcpConnection* connection)
{
    struct sockaddr_storage local;
    struct sockaddr_storage remote;
    int local_len = (int)sizeof(local);
    int remote_len = (int)sizeof(remote);

    if (connection == NULL || !connection->handle_initialized) {
        return;
    }
    if (uv_tcp_getsockname(&connection->handle, (struct sockaddr*)&local, &local_len) == 0) {
        connection->local_family = sl_tcp_endpoint_from_sockaddr(
            (const struct sockaddr*)&local, connection->local_host, &connection->local_port);
    }
    if (uv_tcp_getpeername(&connection->handle, (struct sockaddr*)&remote, &remote_len) == 0) {
        connection->remote_family = sl_tcp_endpoint_from_sockaddr(
            (const struct sockaddr*)&remote, connection->remote_host, &connection->remote_port);
    }
}

static SlStatus sl_tcp_parse_sockaddr(const SlTcpConnectOptions* options,
                                      struct sockaddr_storage* out_addr, SlDiag* out_diag)
{
    char host[SL_TCP_MAX_HOST_BYTES + 1U];
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;

    if (options == NULL || out_addr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_tcp_copy_host_to_cstr(options->host, host)) {
        return sl_tcp_fail(out_diag, SL_DIAG_NET_INVALID_HOST, SL_STATUS_INVALID_ARGUMENT,
                           sl_tcp_literal("network host is invalid"));
    }
    if (options->port == 0U) {
        return sl_tcp_fail(out_diag, SL_DIAG_NET_INVALID_PORT, SL_STATUS_INVALID_ARGUMENT,
                           sl_tcp_literal("network port is invalid"));
    }
    if (uv_ip4_addr(host, (int)options->port, &addr4) == 0) {
        memset(out_addr, 0, sizeof(*out_addr));
        memcpy(out_addr, &addr4, sizeof(addr4));
        return sl_status_ok();
    }
    if (uv_ip6_addr(host, (int)options->port, &addr6) == 0) {
        memset(out_addr, 0, sizeof(*out_addr));
        memcpy(out_addr, &addr6, sizeof(addr6));
        return sl_status_ok();
    }
    return sl_tcp_fail(out_diag, SL_DIAG_NET_DNS_FAILURE, SL_STATUS_UNSUPPORTED,
                       sl_tcp_literal("DNS resolution failed"));
}

static SlStatus sl_tcp_alloc_connection(SlArena* arena, size_t read_capacity, SlTcpConnection** out)
{
    void* memory = NULL;
    void* read_memory = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, sizeof(SlTcpConnection), _Alignof(SlTcpConnection), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (read_capacity == 0U) {
        read_capacity = SL_TCP_DEFAULT_READ_BUFFER_CAPACITY;
    }
    status = sl_arena_alloc(arena, read_capacity, _Alignof(unsigned char), &read_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (SlTcpConnection*)memory;
    **out = (SlTcpConnection){0};
    (*out)->read_buffer = (unsigned char*)read_memory;
    (*out)->read_buffer_capacity = read_capacity;
    (*out)->state = SL_TCP_CONNECTION_CONNECTING;
    return sl_status_ok();
}

SlTcpConnectOptions sl_tcp_connect_options_default(SlStr host, uint16_t port)
{
    SlTcpConnectOptions options = {0};

    options.host = host;
    options.port = port;
    options.read_buffer_capacity = SL_TCP_DEFAULT_READ_BUFFER_CAPACITY;
    return options;
}

SlStatus sl_tcp_client_connect(SlArena* arena, const SlTcpConnectOptions* options,
                               SlTcpConnection** out_connection, SlDiag* out_diag)
{
    SlTcpConnection* connection = NULL;
    struct sockaddr_storage addr;
    SlStatus status;
    int uv_status = 0;

    if (out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    status = sl_tcp_parse_sockaddr(options, &addr, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_tcp_alloc_connection(arena, options->read_buffer_capacity, &connection);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    uv_status = uv_loop_init(&connection->loop);
    if (uv_status != 0) {
        connection->state = SL_TCP_CONNECTION_FAILED;
        return sl_tcp_status_from_uv(uv_status, out_diag, SL_DIAG_NET_BACKEND_UNAVAILABLE,
                                     sl_tcp_literal("TCP backend is unavailable"));
    }
    connection->loop_initialized = true;

    uv_status = uv_tcp_init(&connection->loop, &connection->handle);
    if (uv_status != 0) {
        connection->state = SL_TCP_CONNECTION_FAILED;
        (void)uv_loop_close(&connection->loop);
        return sl_tcp_status_from_uv(uv_status, out_diag, SL_DIAG_NET_BACKEND_UNAVAILABLE,
                                     sl_tcp_literal("TCP backend is unavailable"));
    }
    connection->handle_initialized = true;
    connection->handle.data = connection;

    if (options->no_delay) {
        (void)uv_tcp_nodelay(&connection->handle, 1);
    }
    if (options->keep_alive.enabled) {
        unsigned int delay_seconds = options->keep_alive.delay_ms / 1000U;
        (void)uv_tcp_keepalive(&connection->handle, 1, delay_seconds);
    }

    connection->connect_req.data = connection;
    if (options->has_timeout_ms && options->timeout_ms > 0U) {
        uv_status = uv_timer_init(&connection->loop, &connection->timer);
        if (uv_status != 0) {
            connection->state = SL_TCP_CONNECTION_FAILED;
            uv_close((uv_handle_t*)&connection->handle, sl_tcp_close_cb);
            while (!connection->close_done) {
                (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
            }
            (void)uv_loop_close(&connection->loop);
            return sl_tcp_status_from_uv(uv_status, out_diag, SL_DIAG_NET_BACKEND_UNAVAILABLE,
                                         sl_tcp_literal("TCP backend is unavailable"));
        }
        connection->timer_initialized = true;
        connection->timer.data = connection;
        (void)uv_timer_start(&connection->timer, sl_tcp_timer_cb, options->timeout_ms, 0U);
    }

    uv_status = uv_tcp_connect(&connection->connect_req, &connection->handle,
                               (const struct sockaddr*)&addr, sl_tcp_connect_cb);
    if (uv_status != 0) {
        connection->state = SL_TCP_CONNECTION_FAILED;
        uv_close((uv_handle_t*)&connection->handle, sl_tcp_close_cb);
        while (!connection->close_done) {
            (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
        }
        (void)uv_loop_close(&connection->loop);
        return sl_tcp_status_from_uv(uv_status, out_diag, SL_DIAG_NET_DNS_FAILURE,
                                     sl_tcp_literal("DNS resolution failed"));
    }

    while (!connection->io.done) {
        (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
    }
    if (connection->timer_initialized && !uv_is_closing((uv_handle_t*)&connection->timer)) {
        uv_timer_stop(&connection->timer);
        uv_close((uv_handle_t*)&connection->timer, sl_tcp_timer_close_cb);
        while (connection->timer_initialized) {
            (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
        }
    }
    if (connection->io.status != 0) {
        connection->state =
            connection->io.timeout ? SL_TCP_CONNECTION_FAILED : SL_TCP_CONNECTION_FAILED;
        if (!connection->close_started && !uv_is_closing((uv_handle_t*)&connection->handle)) {
            uv_close((uv_handle_t*)&connection->handle, sl_tcp_close_cb);
            connection->close_started = true;
        }
        while (!connection->close_done) {
            (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
        }
        (void)uv_loop_close(&connection->loop);
        return sl_tcp_status_from_uv(connection->io.status, out_diag, SL_DIAG_NET_DNS_FAILURE,
                                     sl_tcp_literal("TCP connect failed"));
    }

    connection->state = SL_TCP_CONNECTION_CONNECTED;
    sl_tcp_refresh_endpoints(connection);
    *out_connection = connection;
    return sl_status_ok();
}

SlTcpConnectionState sl_tcp_connection_state(const SlTcpConnection* connection)
{
    return connection == NULL ? SL_TCP_CONNECTION_FAILED : connection->state;
}

static SlStatus sl_tcp_endpoint(const SlTcpConnection* connection, bool local,
                                SlNetworkEndpoint* out_endpoint)
{
    const char* host = NULL;
    uint16_t port = 0U;
    SlNetworkAddressFamily family = SL_NETWORK_ADDRESS_UNSPECIFIED;

    if (connection == NULL || out_endpoint == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    host = local ? connection->local_host : connection->remote_host;
    port = local ? connection->local_port : connection->remote_port;
    family = local ? connection->local_family : connection->remote_family;
    *out_endpoint = (SlNetworkEndpoint){family, sl_str_from_cstr(host), port};
    return sl_status_ok();
}

SlStatus sl_tcp_connection_local_endpoint(const SlTcpConnection* connection,
                                          SlNetworkEndpoint* out_endpoint)
{
    return sl_tcp_endpoint(connection, true, out_endpoint);
}

SlStatus sl_tcp_connection_remote_endpoint(const SlTcpConnection* connection,
                                           SlNetworkEndpoint* out_endpoint)
{
    return sl_tcp_endpoint(connection, false, out_endpoint);
}

static SlStatus sl_tcp_require_connected(SlTcpConnection* connection, SlDiag* out_diag)
{
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_TCP_CONNECTION_CONNECTED) {
        return sl_tcp_fail(out_diag, SL_DIAG_NET_CONNECTION_CLOSED, SL_STATUS_INVALID_STATE,
                           sl_tcp_literal("TCP connection is closed"));
    }
    return sl_status_ok();
}

SlStatus sl_tcp_connection_write(SlTcpConnection* connection, SlBytes bytes, SlDiag* out_diag)
{
    SlStatus status = sl_tcp_require_connected(connection, out_diag);
    uv_buf_t buf;
    int uv_status = 0;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (bytes.length != 0U && bytes.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (bytes.length > UINT_MAX) {
        return sl_tcp_fail(out_diag, SL_DIAG_NET_BACKPRESSURE_OVERFLOW, SL_STATUS_CAPACITY_EXCEEDED,
                           sl_tcp_literal("TCP backpressure buffer overflowed"));
    }
    connection->io = (SlTcpIoState){0};
    connection->io.write_data = bytes.ptr;
    connection->io.write_length = bytes.length;
    buf = uv_buf_init((char*)bytes.ptr,
                      bytes.length > UINT_MAX ? UINT_MAX : (unsigned int)bytes.length);
    connection->write_req.data = connection;
    uv_status = uv_write(&connection->write_req, (uv_stream_t*)&connection->handle, &buf, 1U,
                         sl_tcp_write_cb);
    if (uv_status != 0) {
        return sl_tcp_status_from_uv(uv_status, out_diag, SL_DIAG_NET_CONNECTION_CLOSED,
                                     sl_tcp_literal("TCP connection is closed"));
    }
    while (!connection->io.done) {
        (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
    }
    if (connection->io.status != 0) {
        return sl_tcp_status_from_uv(connection->io.status, out_diag, SL_DIAG_NET_CONNECTION_CLOSED,
                                     sl_tcp_literal("TCP connection is closed"));
    }
    return sl_status_ok();
}

SlStatus sl_tcp_connection_write_text(SlTcpConnection* connection, SlStr text, SlDiag* out_diag)
{
    if (text.length != 0U && text.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_tcp_connection_write(
        connection, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length), out_diag);
}

SlStatus sl_tcp_connection_read(SlTcpConnection* connection, SlArena* arena, size_t max_bytes,
                                SlOwnedBytes* out, SlDiag* out_diag)
{
    SlStatus status = sl_tcp_require_connected(connection, out_diag);
    size_t capacity = 0U;
    SlBytes view;
    int uv_status = 0;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (max_bytes == 0U || max_bytes > connection->read_buffer_capacity) {
        capacity = connection->read_buffer_capacity;
    }
    else {
        capacity = max_bytes;
    }
    connection->io = (SlTcpIoState){0};
    connection->io.read_data = connection->read_buffer;
    connection->io.read_capacity = capacity;
    uv_status = uv_read_start((uv_stream_t*)&connection->handle, sl_tcp_alloc_cb, sl_tcp_read_cb);
    if (uv_status != 0) {
        return sl_tcp_status_from_uv(uv_status, out_diag, SL_DIAG_NET_CONNECTION_CLOSED,
                                     sl_tcp_literal("TCP connection is closed"));
    }
    while (!connection->io.done) {
        (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
    }
    if (connection->io.status != 0) {
        if (connection->io.status == UV_EOF) {
            connection->state = SL_TCP_CONNECTION_CLOSED;
        }
        return sl_tcp_status_from_uv(connection->io.status, out_diag, SL_DIAG_NET_CONNECTION_CLOSED,
                                     sl_tcp_literal("TCP connection is closed"));
    }
    view = sl_bytes_from_parts(connection->read_buffer, connection->io.read_length);
    return sl_bytes_copy_to_arena(arena, view, out);
}

SlStatus sl_tcp_connection_read_until(SlTcpConnection* connection, SlArena* arena,
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
        status = sl_tcp_connection_read(connection, arena, 1U, &chunk, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_byte_builder_append_bytes(&builder, sl_owned_bytes_as_view(chunk));
        if (!sl_status_is_ok(status)) {
            return sl_tcp_fail(out_diag, SL_DIAG_NET_BACKPRESSURE_OVERFLOW,
                               SL_STATUS_CAPACITY_EXCEEDED,
                               sl_tcp_literal("TCP backpressure buffer overflowed"));
        }
        view = sl_byte_builder_view(&builder);
        if (sl_bytes_ends_with(view, delimiter)) {
            return sl_bytes_copy_to_arena(arena, view, out);
        }
    }
    return sl_tcp_fail(out_diag, SL_DIAG_NET_BACKPRESSURE_OVERFLOW, SL_STATUS_CAPACITY_EXCEEDED,
                       sl_tcp_literal("TCP backpressure buffer overflowed"));
}

SlStatus sl_tcp_connection_read_line(SlTcpConnection* connection, SlArena* arena, size_t max_bytes,
                                     SlOwnedStr* out, SlDiag* out_diag)
{
    static const unsigned char newline = '\n';
    SlOwnedBytes bytes = {0};
    SlStatus status = sl_tcp_connection_read_until(
        connection, arena, sl_bytes_from_parts(&newline, 1U), max_bytes, &bytes, out_diag);
    size_t length = 0U;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
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

static SlStatus sl_tcp_connection_finish_close(SlTcpConnection* connection,
                                               SlTcpConnectionState state)
{
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state == SL_TCP_CONNECTION_CLOSED ||
        connection->state == SL_TCP_CONNECTION_ABORTED)
    {
        return sl_status_ok();
    }
    connection->state = SL_TCP_CONNECTION_CLOSING;
    if (connection->timer_initialized && !uv_is_closing((uv_handle_t*)&connection->timer)) {
        uv_timer_stop(&connection->timer);
        uv_close((uv_handle_t*)&connection->timer, sl_tcp_timer_close_cb);
    }
    if (connection->handle_initialized && !uv_is_closing((uv_handle_t*)&connection->handle)) {
        uv_close((uv_handle_t*)&connection->handle, sl_tcp_close_cb);
        connection->close_started = true;
    }
    while ((connection->timer_initialized || !connection->close_done) &&
           connection->loop_initialized)
    {
        (void)uv_run(&connection->loop, UV_RUN_DEFAULT);
    }
    if (connection->loop_initialized) {
        (void)uv_loop_close(&connection->loop);
        connection->loop_initialized = false;
    }
    connection->state = state;
    return sl_status_ok();
}

SlStatus sl_tcp_connection_close(SlTcpConnection* connection, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_tcp_connection_finish_close(connection, SL_TCP_CONNECTION_CLOSED);
}

SlStatus sl_tcp_connection_abort(SlTcpConnection* connection, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_tcp_connection_finish_close(connection, SL_TCP_CONNECTION_ABORTED);
}
