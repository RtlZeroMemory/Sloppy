/*
 * Libuv-backed HTTP transport listener foundation.
 *
 * This file owns uv loop, listener, and accepted TCP handles. Public Slop headers expose
 * only Slop-owned transport/backend state plus opaque platform pointers.
 */
#include "sloppy/http_transport.h"

#include "sloppy/checked_math.h"

#include <uv.h>

struct SlHttpPlatformConnection
{
    uv_tcp_t handle;
    SlHttpTransportConnection* owner;
    bool initialized;
    bool closing;
};

struct SlHttpPlatformListener
{
    uv_loop_t loop;
    uv_tcp_t listener;
    uv_tcp_t overflow;
    SlHttpTransportServer* server;
    bool loop_initialized;
    bool listener_initialized;
    bool overflow_initialized;
    bool closing;
    uint32_t bound_port;
};

static SlStr sl_http_transport_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_http_transport_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static void sl_http_transport_clear_diag(SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
}

static SlStatus sl_http_transport_diag(SlDiag* out_diag, SlDiagCode code, SlStatusCode status_code,
                                       SlStr message, SlStr hint)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = code;
        out_diag->message = message;
        if (!sl_str_is_empty(hint)) {
            out_diag->hints[0] = hint;
            out_diag->hint_count = 1U;
        }
    }
    return sl_status_from_code(status_code);
}

static SlStatus sl_http_transport_invalid_config(SlDiag* out_diag, SlStr message)
{
    return sl_http_transport_diag(
        out_diag, SL_DIAG_HTTP_TRANSPORT_CONFIG, SL_STATUS_INVALID_ARGUMENT, message,
        sl_http_transport_literal("use a bounded localhost transport configuration",
                                  sizeof("use a bounded localhost transport configuration") - 1U));
}

static SlStatus sl_http_transport_invalid_state(SlDiag* out_diag)
{
    return sl_http_transport_diag(
        out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
        sl_http_transport_literal("HTTP transport server lifecycle state is invalid",
                                  sizeof("HTTP transport server lifecycle state is invalid") - 1U),
        sl_http_transport_literal("call init/listen/stop/dispose in order",
                                  sizeof("call init/listen/stop/dispose in order") - 1U));
}

static SlStatus sl_http_transport_uv_status(int rc, SlDiag* out_diag, SlDiagCode code,
                                            SlStr message)
{
    SlStatusCode status_code = SL_STATUS_INTERNAL;

    if (rc == 0) {
        return sl_status_ok();
    }
    if (rc == UV_EINVAL) {
        status_code = SL_STATUS_INVALID_ARGUMENT;
    }
    else if (rc == UV_ENOMEM) {
        status_code = SL_STATUS_OUT_OF_MEMORY;
    }
    else if (rc == UV_EADDRINUSE || rc == UV_EADDRNOTAVAIL || rc == UV_EACCES) {
        status_code = SL_STATUS_INVALID_STATE;
    }

    return sl_http_transport_diag(
        out_diag, code, status_code, message,
        sl_http_transport_literal("libuv details stay inside the platform transport boundary",
                                  sizeof("libuv details stay inside the platform transport "
                                         "boundary") -
                                      1U));
}

static SlHttpTransportConfig sl_http_transport_config_defaults(void)
{
    SlHttpTransportConfig config = {0};

    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = SL_HTTP_TRANSPORT_DEFAULT_PORT;
    config.max_connections = SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS;
    config.max_active_requests = SL_HTTP_BACKEND_DEFAULT_MAX_ACTIVE_REQUESTS;
    config.connection_capacity = SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS;
    config.backlog = SL_HTTP_TRANSPORT_DEFAULT_BACKLOG;
    return config;
}

static SlStatus sl_http_transport_normalize_config(const SlHttpTransportConfig* input,
                                                   SlHttpTransportConfig* out, SlDiag* out_diag)
{
    SlHttpTransportConfig config = sl_http_transport_config_defaults();

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (input != NULL) {
        if (!sl_str_is_empty(input->host)) {
            config.host = input->host;
        }
        config.port = input->port;
        config.max_connections = input->max_connections;
        config.max_active_requests = input->max_active_requests;
        config.connection_capacity = input->connection_capacity;
        config.backlog = input->backlog;
    }

    if (!sl_http_transport_str_valid(config.host) || sl_str_is_empty(config.host)) {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal("HTTP transport host is invalid",
                                                sizeof("HTTP transport host is invalid") - 1U));
    }
    if (config.port > 65535U) {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal("HTTP transport port is invalid",
                                                sizeof("HTTP transport port is invalid") - 1U));
    }
    if (config.max_connections == 0U || config.max_active_requests == 0U ||
        config.connection_capacity == 0U || config.connection_capacity < config.max_connections)
    {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal(
                          "HTTP transport connection capacity is invalid",
                          sizeof("HTTP transport connection capacity is invalid") - 1U));
    }
    if (config.backlog <= 0) {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal("HTTP transport backlog is invalid",
                                                sizeof("HTTP transport backlog is invalid") - 1U));
    }

    *out = config;
    return sl_status_ok();
}

static SlStatus sl_http_transport_alloc(SlArena* arena, size_t size, size_t align, void** out)
{
    if (arena == NULL || out == NULL || size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_arena_alloc(arena, size, align, out);
}

static void sl_http_transport_connection_close_cb(uv_handle_t* handle)
{
    SlHttpPlatformConnection* platform = (SlHttpPlatformConnection*)handle->data;

    if (platform == NULL) {
        return;
    }
    platform->closing = false;
    platform->initialized = false;
    if (platform->owner != NULL) {
        platform->owner->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED;
    }
}

static void sl_http_transport_overflow_close_cb(uv_handle_t* handle)
{
    SlHttpPlatformListener* platform = (SlHttpPlatformListener*)handle->data;

    if (platform != NULL) {
        platform->overflow_initialized = false;
    }
}

static SlHttpTransportConnection* sl_http_transport_claim_connection(SlHttpTransportServer* server)
{
    size_t index = 0U;

    if (server == NULL) {
        return NULL;
    }
    for (index = 0U; index < server->connection_capacity; index += 1U) {
        SlHttpTransportConnection* connection = &server->connections[index];
        if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_EMPTY ||
            connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED)
        {
            *connection = (SlHttpTransportConnection){0};
            connection->platform = &server->platform_connections[index];
            connection->platform->owner = connection;
            connection->slot_claimed = true;
            return connection;
        }
    }
    return NULL;
}

static void sl_http_transport_reject_pending(uv_stream_t* listener,
                                             SlHttpPlatformListener* platform)
{
    if (platform == NULL || platform->overflow_initialized) {
        return;
    }
    if (uv_tcp_init(&platform->loop, &platform->overflow) != 0) {
        return;
    }
    platform->overflow_initialized = true;
    platform->overflow.data = platform;
    if (uv_accept(listener, (uv_stream_t*)&platform->overflow) != 0) {
        platform->overflow_initialized = false;
        return;
    }
    uv_close((uv_handle_t*)&platform->overflow, sl_http_transport_overflow_close_cb);
}

static void sl_http_transport_on_connection(uv_stream_t* listener, int status)
{
    SlHttpPlatformListener* platform = NULL;
    SlHttpTransportServer* server = NULL;
    SlHttpTransportConnection* connection = NULL;
    SlStatus accept_status;

    if (listener == NULL) {
        return;
    }
    platform = (SlHttpPlatformListener*)listener->data;
    server = platform == NULL ? NULL : platform->server;
    if (server == NULL) {
        return;
    }
    if (status < 0) {
        server->accept_failures += 1U;
        return;
    }

    connection = sl_http_transport_claim_connection(server);
    if (connection == NULL) {
        server->rejected_connections += 1U;
        sl_http_transport_reject_pending(listener, platform);
        return;
    }

    accept_status = sl_http_backend_accept_connection(&server->backend, &connection->core, NULL);
    if (!sl_status_is_ok(accept_status)) {
        connection->slot_claimed = false;
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED;
        server->rejected_connections += 1U;
        sl_http_transport_reject_pending(listener, platform);
        return;
    }

    if (uv_tcp_init(&platform->loop, &connection->platform->handle) != 0) {
        (void)sl_http_connection_fail(&connection->core, NULL);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        server->accept_failures += 1U;
        return;
    }

    connection->platform->initialized = true;
    connection->platform->closing = false;
    connection->platform->handle.data = connection->platform;
    if (uv_accept(listener, (uv_stream_t*)&connection->platform->handle) != 0) {
        (void)sl_http_connection_fail(&connection->core, NULL);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        connection->platform->closing = true;
        uv_close((uv_handle_t*)&connection->platform->handle,
                 sl_http_transport_connection_close_cb);
        server->accept_failures += 1U;
        return;
    }

    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED;
    server->accepted_connections += 1U;
}

static void sl_http_transport_close_listener(SlHttpPlatformListener* platform)
{
    if (platform == NULL) {
        return;
    }
    if (platform->listener_initialized && !uv_is_closing((uv_handle_t*)&platform->listener)) {
        uv_close((uv_handle_t*)&platform->listener, NULL);
    }
    if (platform->overflow_initialized && !uv_is_closing((uv_handle_t*)&platform->overflow)) {
        uv_close((uv_handle_t*)&platform->overflow, sl_http_transport_overflow_close_cb);
    }
    if (platform->loop_initialized) {
        while (uv_run(&platform->loop, UV_RUN_DEFAULT) != 0) {
        }
    }
    platform->listener_initialized = false;
}

SlStatus sl_http_transport_server_init(SlHttpTransportServer* server, SlArena* arena,
                                       const SlHttpTransportConfig* config, SlDiag* out_diag)
{
    SlHttpBackendOptions backend_options = {0};
    SlStatus status;
    void* memory = NULL;
    size_t connections_bytes = 0U;
    size_t platform_connections_bytes = 0U;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *server = (SlHttpTransportServer){0};
    server->arena = arena;

    status = sl_http_transport_normalize_config(config, &server->config, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_to_arena_nul(arena, server->config.host, &server->host);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    backend_options.max_connections = server->config.max_connections;
    backend_options.max_active_requests = server->config.max_active_requests;
    status = sl_http_backend_init(&server->backend, &backend_options, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_http_transport_alloc(arena, sizeof(SlHttpPlatformListener),
                                     _Alignof(SlHttpPlatformListener), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->platform = (SlHttpPlatformListener*)memory;
    *server->platform = (SlHttpPlatformListener){0};
    server->platform->server = server;

    status = sl_checked_mul_size(sizeof(SlHttpTransportConnection),
                                 server->config.connection_capacity, &connections_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_transport_alloc(arena, connections_bytes, _Alignof(SlHttpTransportConnection),
                                     &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->connections = (SlHttpTransportConnection*)memory;

    status = sl_checked_mul_size(sizeof(SlHttpPlatformConnection),
                                 server->config.connection_capacity, &platform_connections_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_transport_alloc(arena, platform_connections_bytes,
                                     _Alignof(SlHttpPlatformConnection), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->platform_connections = (SlHttpPlatformConnection*)memory;
    server->connection_capacity = server->config.connection_capacity;

    for (size_t index = 0U; index < server->connection_capacity; index += 1U) {
        server->connections[index] = (SlHttpTransportConnection){0};
        server->connections[index].platform = &server->platform_connections[index];
        server->platform_connections[index] = (SlHttpPlatformConnection){0};
    }

    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_CREATED;
    return sl_status_ok();
}

SlStatus sl_http_transport_server_listen(SlHttpTransportServer* server, SlDiag* out_diag)
{
    struct sockaddr_in address;
    int rc = 0;
    SlStatus status;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || server->platform == NULL || server->host.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING) {
        return sl_http_transport_invalid_state(out_diag);
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_CREATED &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED)
    {
        return sl_http_transport_invalid_state(out_diag);
    }

    rc = uv_ip4_addr(server->host.ptr, (int)server->config.port, &address);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_TRANSPORT_CONFIG,
            sl_http_transport_literal("HTTP transport bind address is invalid",
                                      sizeof("HTTP transport bind address is invalid") - 1U));
    }

    rc = uv_loop_init(&server->platform->loop);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_LISTEN_FAILED,
            sl_http_transport_literal("HTTP transport loop initialization failed",
                                      sizeof("HTTP transport loop initialization failed") - 1U));
    }
    server->platform->loop_initialized = true;

    rc = uv_tcp_init(&server->platform->loop, &server->platform->listener);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_BIND_FAILED,
            sl_http_transport_literal("HTTP transport listener initialization failed",
                                      sizeof("HTTP transport listener initialization failed") -
                                          1U));
    }
    server->platform->listener_initialized = true;
    server->platform->listener.data = server->platform;

    rc = uv_tcp_bind(&server->platform->listener, (const struct sockaddr*)&address, 0U);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        sl_http_transport_close_listener(server->platform);
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_BIND_FAILED,
            sl_http_transport_literal("HTTP transport bind failed",
                                      sizeof("HTTP transport bind failed") - 1U));
    }

    rc = uv_listen((uv_stream_t*)&server->platform->listener, server->config.backlog,
                   sl_http_transport_on_connection);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        sl_http_transport_close_listener(server->platform);
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_LISTEN_FAILED,
            sl_http_transport_literal("HTTP transport listen failed",
                                      sizeof("HTTP transport listen failed") - 1U));
    }

    status = sl_http_backend_start(&server->backend, server->platform, out_diag);
    if (!sl_status_is_ok(status)) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        sl_http_transport_close_listener(server->platform);
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return status;
    }

    {
        struct sockaddr_storage bound = {0};
        int length = (int)sizeof(bound);
        if (uv_tcp_getsockname(&server->platform->listener, (struct sockaddr*)&bound, &length) == 0)
        {
            const struct sockaddr_in* in = (const struct sockaddr_in*)&bound;
            server->platform->bound_port = (uint32_t)ntohs(in->sin_port);
        }
        else {
            server->platform->bound_port = server->config.port;
        }
    }

    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING;
    return sl_status_ok();
}

SlStatus sl_http_transport_server_poll(SlHttpTransportServer* server, SlDiag* out_diag)
{
    size_t previous_accept_failures = 0U;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || server->platform == NULL || !server->platform->loop_initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING)
    {
        return sl_http_transport_invalid_state(out_diag);
    }
    previous_accept_failures = server->accept_failures;
    (void)uv_run(&server->platform->loop, UV_RUN_NOWAIT);
    if (server->accept_failures != previous_accept_failures) {
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_ACCEPT_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP transport accept failed",
                                      sizeof("HTTP transport accept failed") - 1U),
            sl_http_transport_literal("the pending connection was not admitted",
                                      sizeof("the pending connection was not admitted") - 1U));
    }
    return sl_status_ok();
}

SlStatus sl_http_transport_connection_close(SlHttpTransportConnection* connection, SlDiag* out_diag)
{
    sl_http_transport_clear_diag(out_diag);
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED ||
        connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_EMPTY)
    {
        return sl_status_ok();
    }
    (void)sl_http_connection_close(&connection->core, out_diag);
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING;
    if (connection->platform != NULL && connection->platform->initialized &&
        !connection->platform->closing)
    {
        connection->platform->closing = true;
        uv_close((uv_handle_t*)&connection->platform->handle,
                 sl_http_transport_connection_close_cb);
    }
    else {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED;
    }
    connection->slot_claimed = false;
    return sl_status_ok();
}

SlStatus sl_http_transport_server_stop(SlHttpTransportServer* server, SlDiag* out_diag)
{
    SlStatus status = sl_status_ok();

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_CREATED ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED)
    {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED;
        return sl_status_ok();
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_ERROR)
    {
        return sl_http_transport_invalid_state(out_diag);
    }

    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING;
    for (size_t index = 0U; index < server->connection_capacity; index += 1U) {
        (void)sl_http_transport_connection_close(&server->connections[index], NULL);
    }

    if (server->backend.state == SL_HTTP_BACKEND_STATE_STARTED ||
        server->backend.state == SL_HTTP_BACKEND_STATE_STOPPING)
    {
        status = sl_http_backend_stop(&server->backend, out_diag);
    }

    if (server->platform != NULL) {
        sl_http_transport_close_listener(server->platform);
        if (server->platform->loop_initialized) {
            (void)uv_loop_close(&server->platform->loop);
            server->platform->loop_initialized = false;
        }
    }
    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED;
    return status;
}

SlStatus sl_http_transport_server_dispose(SlHttpTransportServer* server, SlDiag* out_diag)
{
    SlStatus status;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED) {
        return sl_status_ok();
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_CREATED ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_ERROR)
    {
        status = sl_http_transport_server_stop(server, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_http_backend_dispose(&server->backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED;
    return sl_status_ok();
}

SlHttpTransportServerState sl_http_transport_server_state(const SlHttpTransportServer* server)
{
    return server == NULL ? SL_HTTP_TRANSPORT_SERVER_STATE_NONE : server->state;
}

uint32_t sl_http_transport_server_bound_port(const SlHttpTransportServer* server)
{
    if (server == NULL || server->platform == NULL) {
        return 0U;
    }
    return server->platform->bound_port;
}

size_t sl_http_transport_server_active_connections(const SlHttpTransportServer* server)
{
    return server == NULL ? 0U : server->backend.active_connections;
}
