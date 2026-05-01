/*
 * Libuv-backed HTTP transport listener foundation.
 *
 * This file owns uv loop, listener, and accepted TCP handles. Public Slop headers expose
 * only Slop-owned transport/backend state plus opaque platform pointers.
 */
#include "sloppy/http_transport.h"

#include "sloppy/checked_math.h"
#include "sloppy/http_response.h"

#include <limits.h>
#include <uv.h>

struct SlHttpPlatformConnection
{
    uv_tcp_t handle;
    uv_write_t write;
    uv_timer_t header_timer;
    uv_timer_t body_timer;
    uv_timer_t request_timer;
    uv_timer_t write_timer;
    SlHttpTransportConnection* owner;
    unsigned char* read_buffer;
    size_t read_buffer_size;
    bool initialized;
    bool header_timer_initialized;
    bool body_timer_initialized;
    bool request_timer_initialized;
    bool write_timer_initialized;
    bool closing;
    bool reading;
    bool writing;
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
    config.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    config.parse.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    config.parse.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    config.parse.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    config.parse.max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
    config.parse.max_body_length = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
    config.max_request_head_bytes = SL_HTTP_TRANSPORT_DEFAULT_MAX_REQUEST_HEAD_BYTES;
    config.request_arena_bytes = SL_HTTP_TRANSPORT_DEFAULT_REQUEST_ARENA_BYTES;
    config.read_chunk_bytes = SL_HTTP_TRANSPORT_DEFAULT_READ_CHUNK_BYTES;
    config.max_response_bytes = SL_HTTP_TRANSPORT_DEFAULT_RESPONSE_BYTES;
    config.header_read_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_HEADER_READ_TIMEOUT_MS;
    config.body_read_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_BODY_READ_TIMEOUT_MS;
    config.request_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_REQUEST_TIMEOUT_MS;
    config.write_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_WRITE_TIMEOUT_MS;
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
        config.parse.max_headers =
            input->parse.max_headers == 0U ? config.parse.max_headers : input->parse.max_headers;
        config.parse.max_target_length = input->parse.max_target_length == 0U
                                             ? config.parse.max_target_length
                                             : input->parse.max_target_length;
        config.parse.max_header_name_length = input->parse.max_header_name_length == 0U
                                                  ? config.parse.max_header_name_length
                                                  : input->parse.max_header_name_length;
        config.parse.max_header_value_length = input->parse.max_header_value_length == 0U
                                                   ? config.parse.max_header_value_length
                                                   : input->parse.max_header_value_length;
        config.parse.max_total_header_bytes = input->parse.max_total_header_bytes == 0U
                                                  ? config.parse.max_total_header_bytes
                                                  : input->parse.max_total_header_bytes;
        config.parse.max_body_length = input->parse.max_body_length == 0U
                                           ? config.parse.max_body_length
                                           : input->parse.max_body_length;
        config.max_request_head_bytes = input->max_request_head_bytes == 0U
                                            ? config.max_request_head_bytes
                                            : input->max_request_head_bytes;
        config.request_arena_bytes = input->request_arena_bytes == 0U ? config.request_arena_bytes
                                                                      : input->request_arena_bytes;
        config.read_chunk_bytes =
            input->read_chunk_bytes == 0U ? config.read_chunk_bytes : input->read_chunk_bytes;
        config.max_response_bytes =
            input->max_response_bytes == 0U ? config.max_response_bytes : input->max_response_bytes;
        config.header_read_timeout_ms = input->header_read_timeout_ms == 0U
                                            ? config.header_read_timeout_ms
                                            : input->header_read_timeout_ms;
        config.body_read_timeout_ms = input->body_read_timeout_ms == 0U
                                          ? config.body_read_timeout_ms
                                          : input->body_read_timeout_ms;
        config.request_timeout_ms =
            input->request_timeout_ms == 0U ? config.request_timeout_ms : input->request_timeout_ms;
        config.write_timeout_ms =
            input->write_timeout_ms == 0U ? config.write_timeout_ms : input->write_timeout_ms;
        config.connection_capacity = input->connection_capacity;
        config.backlog = input->backlog;
        config.on_request_ready = input->on_request_ready;
        config.on_request_ready_user = input->on_request_ready_user;
        config.dispatch = input->dispatch;
        config.dispatch_user = input->dispatch_user;
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
    if (config.max_request_head_bytes == 0U || config.request_arena_bytes == 0U ||
        config.read_chunk_bytes == 0U || config.max_response_bytes == 0U)
    {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal(
                          "HTTP transport request buffer capacity is invalid",
                          sizeof("HTTP transport request buffer capacity is invalid") - 1U));
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

static uint16_t sl_http_transport_status_for_failure(SlStatus status, const SlDiag* diag)
{
    SlStatusCode code = sl_status_code(status);

    if (diag != NULL) {
        if (diag->code == SL_DIAG_HTTP_ROUTE_NOT_FOUND) {
            return 404U;
        }
        if (diag->code == SL_DIAG_HTTP_UNSUPPORTED_METHOD) {
            return 405U;
        }
        if (diag->code == SL_DIAG_HTTP_BODY_LIMIT || diag->code == SL_DIAG_HTTP_HEADER_LIMIT ||
            diag->code == SL_DIAG_HTTP_HEADER_BYTES_LIMIT ||
            diag->code == SL_DIAG_HTTP_TARGET_LIMIT ||
            diag->code == SL_DIAG_HTTP_HEADER_NAME_LIMIT ||
            diag->code == SL_DIAG_HTTP_HEADER_VALUE_LIMIT)
        {
            return 413U;
        }
        if (diag->code == SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE) {
            return 415U;
        }
        if (diag->code == SL_DIAG_HTTP_UNSUPPORTED_BODY) {
            return 501U;
        }
        if (diag->code == SL_DIAG_INVALID_HTTP_REQUEST || diag->code == SL_DIAG_MALFORMED_JSON ||
            diag->code == SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED)
        {
            return 400U;
        }
    }

    if (code == SL_STATUS_CAPACITY_EXCEEDED) {
        return 413U;
    }
    if (code == SL_STATUS_UNSUPPORTED) {
        return 501U;
    }
    if (code == SL_STATUS_INVALID_ARGUMENT) {
        return 400U;
    }
    return 500U;
}

static SlStr sl_http_transport_body_for_status(uint16_t status)
{
    switch (status) {
    case 400U:
        return sl_http_transport_literal("Malformed HTTP request\n",
                                         sizeof("Malformed HTTP request\n") - 1U);
    case 404U:
        return sl_http_transport_literal("Not Found\n", sizeof("Not Found\n") - 1U);
    case 405U:
        return sl_http_transport_literal("Method Not Allowed\n",
                                         sizeof("Method Not Allowed\n") - 1U);
    case 408U:
        return sl_http_transport_literal("Request Timeout\n", sizeof("Request Timeout\n") - 1U);
    case 413U:
        return sl_http_transport_literal("Payload Too Large\n", sizeof("Payload Too Large\n") - 1U);
    case 415U:
        return sl_http_transport_literal("Unsupported Media Type\n",
                                         sizeof("Unsupported Media Type\n") - 1U);
    case 501U:
        return sl_http_transport_literal("Request body framing is not supported\n",
                                         sizeof("Request body framing is not supported\n") - 1U);
    default:
        return sl_http_transport_literal("Sloppy handler failed\n",
                                         sizeof("Sloppy handler failed\n") - 1U);
    }
}

static void sl_http_transport_store_diag(SlHttpTransportConnection* connection, const SlDiag* diag)
{
    if (connection != NULL && diag != NULL) {
        connection->last_diag = *diag;
    }
}

static SlStatus sl_http_transport_connection_diag(SlHttpTransportConnection* connection,
                                                  SlDiag* out_diag, SlDiagCode code,
                                                  SlStatusCode status_code, SlStr message,
                                                  SlStr hint)
{
    SlDiag diag = {0};
    SlStatus status = sl_http_transport_diag(&diag, code, status_code, message, hint);

    sl_http_transport_store_diag(connection, &diag);
    if (out_diag != NULL) {
        *out_diag = diag;
    }
    return status;
}

static int sl_http_transport_ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static bool sl_http_transport_str_iequal(SlStr left, SlStr right)
{
    size_t index = 0U;

    if ((left.ptr == NULL && left.length != 0U) || (right.ptr == NULL && right.length != 0U) ||
        left.length != right.length)
    {
        return false;
    }

    for (index = 0U; index < left.length; index += 1U) {
        if (sl_http_transport_ascii_lower((unsigned char)left.ptr[index]) !=
            sl_http_transport_ascii_lower((unsigned char)right.ptr[index]))
        {
            return false;
        }
    }
    return true;
}

static SlStr sl_http_transport_trim_ascii_space(SlStr value)
{
    size_t begin = 0U;
    size_t end = value.length;

    if (value.ptr == NULL) {
        return sl_str_empty();
    }
    while (begin < end && (value.ptr[begin] == ' ' || value.ptr[begin] == '\t')) {
        begin += 1U;
    }
    while (end > begin && (value.ptr[end - 1U] == ' ' || value.ptr[end - 1U] == '\t')) {
        end -= 1U;
    }
    return sl_str_from_parts(value.ptr + begin, end - begin);
}

static bool sl_http_transport_header_value(const SlHttpRequestHead* head, SlStr name,
                                           SlStr* out_value)
{
    size_t index = 0U;

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (head == NULL || out_value == NULL || (head->header_count != 0U && head->headers == NULL)) {
        return false;
    }
    for (index = 0U; index < head->header_count; index += 1U) {
        if (sl_http_transport_str_iequal(head->headers[index].name, name)) {
            *out_value = head->headers[index].value;
            return true;
        }
    }
    return false;
}

static bool sl_http_transport_has_header(const SlHttpRequestHead* head, SlStr name)
{
    SlStr value = {0};
    return sl_http_transport_header_value(head, name, &value);
}

static SlHttpTransportServer*
sl_http_transport_connection_server(const SlHttpTransportConnection* connection);
static SlStatus sl_http_transport_start_timer(SlHttpPlatformConnection* platform, uv_timer_t* timer,
                                              bool* initialized, uint64_t timeout_ms,
                                              uv_timer_cb callback);
static void sl_http_transport_stop_timer(uv_timer_t* timer, bool* initialized);
static void sl_http_transport_write_timeout_cb(uv_timer_t* timer);

static bool sl_http_transport_request_terminal(SlHttpRequestState state)
{
    return state == SL_HTTP_REQUEST_STATE_CLOSED || state == SL_HTTP_REQUEST_STATE_COMPLETED ||
           state == SL_HTTP_REQUEST_STATE_CANCELLED || state == SL_HTTP_REQUEST_STATE_TIMED_OUT ||
           state == SL_HTTP_REQUEST_STATE_FAILED;
}

static void sl_http_transport_write_cb(uv_write_t* request, int status)
{
    SlHttpPlatformConnection* platform =
        request == NULL ? NULL : (SlHttpPlatformConnection*)request->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlDiag diag = {0};

    if (platform != NULL) {
        platform->writing = false;
        sl_http_transport_stop_timer(&platform->write_timer, &platform->write_timer_initialized);
        sl_http_transport_stop_timer(&platform->request_timer,
                                     &platform->request_timer_initialized);
    }
    if (connection == NULL) {
        return;
    }

    connection->write_completed = true;
    if (connection->request_started &&
        !sl_http_transport_request_terminal(connection->request.state))
    {
        if (status != 0) {
            (void)sl_http_transport_connection_diag(
                connection, &diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INTERNAL,
                sl_http_transport_literal("HTTP transport response write failed",
                                          sizeof("HTTP transport response write failed") - 1U),
                sl_http_transport_literal(
                    "socket details stay inside the platform boundary",
                    sizeof("socket details stay inside the platform boundary") - 1U));
            (void)sl_http_request_fail(&connection->request, NULL);
        }
        else {
            (void)sl_http_request_complete(&connection->request, NULL);
        }
    }

    (void)sl_http_transport_connection_close(connection, NULL);
}

static SlStatus sl_http_transport_write_response(SlHttpTransportConnection* connection,
                                                 const SlHttpResponse* response, SlDiag* out_diag)
{
    SlStatus status;
    SlBytes bytes = {0};
    uv_buf_t buffer;
    int rc = 0;

    if (connection == NULL || response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_DISPATCHING ||
        connection->request.state != SL_HTTP_REQUEST_STATE_DISPATCHING)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport dispatch state is invalid",
                                      sizeof("HTTP transport dispatch state is invalid") - 1U),
            sl_http_transport_literal("write responses only after one request dispatch",
                                      sizeof("write responses only after one request dispatch") -
                                          1U));
    }

    status = sl_http_request_begin_write(&connection->request, out_diag);
    if (!sl_status_is_ok(status)) {
        sl_http_transport_store_diag(connection, out_diag);
        return status;
    }
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE;

    status = sl_http_response_write(response, connection->response_storage,
                                    connection->response_storage_size, &bytes);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            sl_status_code(status),
            sl_http_transport_literal("HTTP response serialization failed",
                                      sizeof("HTTP response serialization failed") - 1U),
            sl_http_transport_literal("response bytes must fit the configured transport buffer",
                                      sizeof("response bytes must fit the configured transport "
                                             "buffer") -
                                          1U));
    }
    if (bytes.length > UINT_MAX) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP response serialization failed",
                                      sizeof("HTTP response serialization failed") - 1U),
            sl_http_transport_literal("response bytes exceed the platform write boundary",
                                      sizeof("response bytes exceed the platform write boundary") -
                                          1U));
    }

    connection->response_length = bytes.length;
    if (connection->platform == NULL || !connection->platform->initialized ||
        connection->platform->closing)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport response write cannot start",
                                      sizeof("HTTP transport response write cannot start") - 1U),
            sl_http_transport_literal("the connection is already closing",
                                      sizeof("the connection is already closing") - 1U));
    }

    buffer = uv_buf_init((char*)connection->response_storage, (unsigned int)bytes.length);
    connection->platform->write.data = connection->platform;
    {
        SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
        status =
            sl_http_transport_start_timer(connection->platform, &connection->platform->write_timer,
                                          &connection->platform->write_timer_initialized,
                                          server == NULL ? 0U : server->config.write_timeout_ms,
                                          sl_http_transport_write_timeout_cb);
    }
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, sl_status_code(status),
            sl_http_transport_literal("HTTP transport write timeout could not start",
                                      sizeof("HTTP transport write timeout could not start") - 1U),
            sl_http_transport_literal("the response write was not queued",
                                      sizeof("the response write was not queued") - 1U));
    }
    connection->platform->writing = true;
    connection->write_started = true;
    rc = uv_write(&connection->platform->write, (uv_stream_t*)&connection->platform->handle,
                  &buffer, 1U, sl_http_transport_write_cb);
    if (rc != 0) {
        connection->platform->writing = false;
        sl_http_transport_stop_timer(&connection->platform->write_timer,
                                     &connection->platform->write_timer_initialized);
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_WRITE_FAILED,
            sl_http_transport_literal("HTTP transport response write failed to start",
                                      sizeof("HTTP transport response write failed to start") -
                                          1U));
    }

    return sl_status_ok();
}

static SlStatus sl_http_transport_write_error_response(SlHttpTransportConnection* connection,
                                                       uint16_t status_code, SlDiag* out_diag)
{
    SlHttpResponse response =
        sl_http_response_text(status_code, sl_http_transport_body_for_status(status_code));
    SlBytes bytes = {0};
    uv_buf_t buffer;

    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->request_started &&
        connection->request.state == SL_HTTP_REQUEST_STATE_DISPATCHING)
    {
        return sl_http_transport_write_response(connection, &response, out_diag);
    }

    if (connection->platform == NULL || !connection->platform->initialized ||
        connection->platform->closing || connection->platform->writing)
    {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (!sl_status_is_ok(sl_http_response_write(&response, connection->response_storage,
                                                connection->response_storage_size, &bytes)) ||
        bytes.length > UINT_MAX)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP response serialization failed",
                                      sizeof("HTTP response serialization failed") - 1U),
            sl_http_transport_literal("response bytes must fit the configured transport buffer",
                                      sizeof("response bytes must fit the configured transport "
                                             "buffer") -
                                          1U));
    }

    buffer = uv_buf_init((char*)connection->response_storage, (unsigned int)bytes.length);
    connection->platform->write.data = connection->platform;
    {
        SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
        SlStatus timer_status =
            sl_http_transport_start_timer(connection->platform, &connection->platform->write_timer,
                                          &connection->platform->write_timer_initialized,
                                          server == NULL ? 0U : server->config.write_timeout_ms,
                                          sl_http_transport_write_timeout_cb);
        if (!sl_status_is_ok(timer_status)) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, sl_status_code(timer_status),
                sl_http_transport_literal("HTTP transport write timeout could not start",
                                          sizeof("HTTP transport write timeout could not start") -
                                              1U),
                sl_http_transport_literal("the response write was not queued",
                                          sizeof("the response write was not queued") - 1U));
        }
    }
    connection->platform->writing = true;
    connection->write_started = true;
    connection->response_length = bytes.length;
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE;
    if (uv_write(&connection->platform->write, (uv_stream_t*)&connection->platform->handle, &buffer,
                 1U, sl_http_transport_write_cb) != 0)
    {
        connection->platform->writing = false;
        sl_http_transport_stop_timer(&connection->platform->write_timer,
                                     &connection->platform->write_timer_initialized);
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP transport response write failed to start",
                                      sizeof("HTTP transport response write failed to start") - 1U),
            sl_http_transport_literal("socket details stay inside the platform boundary",
                                      sizeof("socket details stay inside the platform boundary") -
                                          1U));
    }
    return sl_status_ok();
}

static void sl_http_transport_timer_close_cb(uv_handle_t* handle)
{
    (void)handle;
}

static void sl_http_transport_stop_timer(uv_timer_t* timer, bool* initialized)
{
    if (timer == NULL || initialized == NULL || !*initialized) {
        return;
    }
    (void)uv_timer_stop(timer);
}

static void sl_http_transport_close_timer(uv_timer_t* timer, bool* initialized)
{
    if (timer == NULL || initialized == NULL || !*initialized) {
        return;
    }
    (void)uv_timer_stop(timer);
    if (!uv_is_closing((uv_handle_t*)timer)) {
        uv_close((uv_handle_t*)timer, sl_http_transport_timer_close_cb);
    }
    *initialized = false;
}

static void sl_http_transport_stop_connection_timers(SlHttpPlatformConnection* platform)
{
    if (platform == NULL) {
        return;
    }
    sl_http_transport_stop_timer(&platform->header_timer, &platform->header_timer_initialized);
    sl_http_transport_stop_timer(&platform->body_timer, &platform->body_timer_initialized);
    sl_http_transport_stop_timer(&platform->request_timer, &platform->request_timer_initialized);
    sl_http_transport_stop_timer(&platform->write_timer, &platform->write_timer_initialized);
}

static void sl_http_transport_close_connection_timers(SlHttpPlatformConnection* platform)
{
    if (platform == NULL) {
        return;
    }
    sl_http_transport_close_timer(&platform->header_timer, &platform->header_timer_initialized);
    sl_http_transport_close_timer(&platform->body_timer, &platform->body_timer_initialized);
    sl_http_transport_close_timer(&platform->request_timer, &platform->request_timer_initialized);
    sl_http_transport_close_timer(&platform->write_timer, &platform->write_timer_initialized);
}

static SlStatus sl_http_transport_start_timer(SlHttpPlatformConnection* platform, uv_timer_t* timer,
                                              bool* initialized, uint64_t timeout_ms,
                                              uv_timer_cb callback)
{
    if (platform == NULL || timer == NULL || initialized == NULL || callback == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!*initialized) {
        int rc = uv_timer_init(platform->handle.loop, timer);
        if (rc != 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        timer->data = platform;
        *initialized = true;
    }
    (void)uv_timer_stop(timer);
    if (timeout_ms == 0U) {
        return sl_status_ok();
    }
    if (uv_timer_start(timer, callback, timeout_ms, 0U) != 0) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_ok();
}

static bool sl_http_transport_connection_terminal(const SlHttpTransportConnection* connection)
{
    return connection == NULL || connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING ||
           connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED ||
           connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
}

static SlHttpTransportServer*
sl_http_transport_connection_server(const SlHttpTransportConnection* connection)
{
    if (connection == NULL || connection->core.backend == NULL ||
        connection->core.backend->listener.platform == NULL)
    {
        return NULL;
    }
    return ((SlHttpPlatformListener*)connection->core.backend->listener.platform)->server;
}

static void sl_http_transport_timeout_connection(SlHttpTransportConnection* connection, SlStr phase)
{
    SlDiag diag = {0};
    SlDiag write_diag = {0};

    if (sl_http_transport_connection_terminal(connection)) {
        return;
    }
    (void)sl_http_transport_connection_diag(
        connection, &diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, SL_STATUS_DEADLINE_EXCEEDED,
        sl_http_transport_literal("HTTP transport request timed out",
                                  sizeof("HTTP transport request timed out") - 1U),
        phase);
    if (connection->platform != NULL && connection->platform->reading) {
        (void)uv_read_stop((uv_stream_t*)&connection->platform->handle);
        connection->platform->reading = false;
    }
    if (connection->request_started && connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_COMPLETED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT &&
        connection->request.state != SL_HTTP_REQUEST_STATE_FAILED)
    {
        (void)sl_http_request_timeout(&connection->request, phase, NULL);
    }
    sl_http_transport_stop_connection_timers(connection->platform);
    if (connection->platform != NULL && connection->platform->initialized &&
        !connection->platform->closing && !connection->platform->writing &&
        connection->response_storage != NULL && connection->response_storage_size != 0U)
    {
        if (sl_status_is_ok(sl_http_transport_write_error_response(connection, 408U, &write_diag)))
        {
            return;
        }
    }
    (void)sl_http_transport_connection_close(connection, NULL);
}

static void sl_http_transport_header_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    sl_http_transport_timeout_connection(
        platform == NULL ? NULL : platform->owner,
        sl_http_transport_literal("HTTP transport header read timeout",
                                  sizeof("HTTP transport header read timeout") - 1U));
}

static void sl_http_transport_body_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    sl_http_transport_timeout_connection(
        platform == NULL ? NULL : platform->owner,
        sl_http_transport_literal("HTTP transport body read timeout",
                                  sizeof("HTTP transport body read timeout") - 1U));
}

static void sl_http_transport_request_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    sl_http_transport_timeout_connection(
        platform == NULL ? NULL : platform->owner,
        sl_http_transport_literal("HTTP transport request timeout",
                                  sizeof("HTTP transport request timeout") - 1U));
}

static void sl_http_transport_write_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlDiag diag = {0};

    if (sl_http_transport_connection_terminal(connection)) {
        return;
    }
    (void)sl_http_transport_connection_diag(
        connection, &diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_DEADLINE_EXCEEDED,
        sl_http_transport_literal("HTTP transport response write timed out",
                                  sizeof("HTTP transport response write timed out") - 1U),
        sl_http_transport_literal("late write completion is cleanup-only",
                                  sizeof("late write completion is cleanup-only") - 1U));
    if (connection->request_started) {
        (void)sl_http_request_timeout(
            &connection->request,
            sl_http_transport_literal("HTTP transport write timeout",
                                      sizeof("HTTP transport write timeout") - 1U),
            NULL);
    }
    (void)sl_http_transport_connection_close(connection, NULL);
}

static bool sl_http_transport_parse_size_decimal(SlStr value, size_t* out)
{
    size_t index = 0U;
    size_t result = 0U;

    if (out == NULL) {
        return false;
    }
    *out = 0U;
    value = sl_http_transport_trim_ascii_space(value);
    if (value.ptr == NULL || value.length == 0U) {
        return false;
    }
    for (index = 0U; index < value.length; index += 1U) {
        size_t next = 0U;
        if (value.ptr[index] < '0' || value.ptr[index] > '9') {
            return false;
        }
        if (!sl_status_is_ok(sl_checked_mul_size(result, 10U, &next))) {
            return false;
        }
        if (!sl_status_is_ok(sl_checked_add_size(next, (size_t)(value.ptr[index] - '0'), &next))) {
            return false;
        }
        result = next;
    }
    *out = result;
    return true;
}

static bool sl_http_transport_header_line_has_name(SlStr line, SlStr name, SlStr* out_value)
{
    size_t index = 0U;
    SlStr line_name = {0};
    SlStr line_value = {0};

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (line.ptr == NULL || name.ptr == NULL) {
        return false;
    }
    while (index < line.length && line.ptr[index] != ':') {
        index += 1U;
    }
    if (index == line.length) {
        return false;
    }
    line_name = sl_http_transport_trim_ascii_space(sl_str_from_parts(line.ptr, index));
    line_value = sl_http_transport_trim_ascii_space(
        sl_str_from_parts(line.ptr + index + 1U, line.length - index - 1U));
    if (!sl_http_transport_str_iequal(line_name, name)) {
        return false;
    }
    if (out_value != NULL) {
        *out_value = line_value;
    }
    return true;
}

static bool sl_http_transport_head_header_value(SlBytes head, SlStr name, SlStr* out_value,
                                                size_t* out_count)
{
    size_t cursor = 0U;
    size_t line_start = 0U;
    size_t count = 0U;
    bool found = false;

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (head.ptr == NULL || name.ptr == NULL) {
        return false;
    }
    while (cursor + 1U < head.length) {
        if (head.ptr[cursor] == '\r' && head.ptr[cursor + 1U] == '\n') {
            SlStr line = sl_str_from_parts((const char*)head.ptr + line_start, cursor - line_start);
            SlStr value = {0};
            if (line_start != 0U && sl_http_transport_header_line_has_name(line, name, &value)) {
                if (out_value != NULL && !found) {
                    *out_value = value;
                }
                count += 1U;
                found = true;
            }
            cursor += 2U;
            line_start = cursor;
            if (cursor + 1U < head.length && head.ptr[cursor] == '\r' &&
                head.ptr[cursor + 1U] == '\n')
            {
                break;
            }
            continue;
        }
        cursor += 1U;
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return found;
}

static size_t sl_http_transport_find_header_end(SlBytes bytes)
{
    size_t index = 0U;

    if (bytes.ptr == NULL || bytes.length < 4U) {
        return 0U;
    }
    for (index = 0U; index + 3U < bytes.length; index += 1U) {
        if (bytes.ptr[index] == '\r' && bytes.ptr[index + 1U] == '\n' &&
            bytes.ptr[index + 2U] == '\r' && bytes.ptr[index + 3U] == '\n')
        {
            return index + 4U;
        }
    }
    return 0U;
}

static SlStatus sl_http_transport_append_bytes(SlHttpTransportConnection* connection, SlBytes bytes,
                                               SlDiag* out_diag)
{
    SlStatus status;

    if (connection == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_byte_builder_append_bytes(&connection->accumulation_builder, bytes);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_BODY_LIMIT, SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP transport request body is too large",
                                      sizeof("HTTP transport request body is too large") - 1U),
            sl_http_transport_literal("request bytes are accumulated only up to configured caps",
                                      sizeof("request bytes are accumulated only up to configured "
                                             "caps") -
                                          1U));
    }
    connection->accumulation_length = sl_byte_builder_length(&connection->accumulation_builder);
    return sl_status_ok();
}

static SlStatus sl_http_transport_dispatch_ready(SlHttpTransportConnection* connection,
                                                 SlHttpTransportServer* server, SlDiag* out_diag)
{
    SlStatus status;
    SlHttpResponse response = {0};
    SlDiag dispatch_diag = {0};
    uint16_t safe_status = 500U;

    if (connection == NULL || server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_REQUEST_READY ||
        connection->request.state != SL_HTTP_REQUEST_STATE_READING)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport dispatch state is invalid",
                                      sizeof("HTTP transport dispatch state is invalid") - 1U),
            sl_http_transport_literal("dispatch only one parsed request-ready connection",
                                      sizeof("dispatch only one parsed request-ready connection") -
                                          1U));
    }
    if (server->config.dispatch == NULL) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_DISPATCH_FAILED, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport dispatch callback is not configured",
                                      sizeof("HTTP transport dispatch callback is not configured") -
                                          1U),
            sl_http_transport_literal("configure the runtime dispatch hook before listening",
                                      sizeof("configure the runtime dispatch hook before "
                                             "listening") -
                                          1U));
    }

    status = sl_http_request_begin_dispatch(&connection->request, out_diag);
    if (!sl_status_is_ok(status)) {
        sl_http_transport_store_diag(connection, out_diag);
        return status;
    }
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_DISPATCHING;

    status = server->config.dispatch(connection, &connection->request_arena, &connection->request,
                                     &response, &dispatch_diag, server->config.dispatch_user);
    if (!sl_status_is_ok(status)) {
        safe_status = sl_http_transport_status_for_failure(status, &dispatch_diag);
        sl_http_transport_store_diag(connection, &dispatch_diag);
        response =
            sl_http_response_text(safe_status, sl_http_transport_body_for_status(safe_status));
    }

    status = sl_http_transport_write_response(connection, &response, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(&connection->request, NULL);
        return status;
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_parse_accumulated(SlHttpTransportConnection* connection,
                                                    SlDiag* out_diag)
{
    SlStatus status;
    SlStr content_length = {0};
    SlStr content_type = {0};
    SlBytes full_message = {0};
    SlBytes body = {0};

    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!connection->request_started) {
        status = sl_http_request_begin(&connection->core, &connection->request_arena,
                                       &connection->request, out_diag);
        if (!sl_status_is_ok(status)) {
            if (out_diag != NULL) {
                sl_http_transport_store_diag(connection, out_diag);
            }
            return status;
        }
        connection->request_started = true;
    }

    full_message = sl_bytes_from_parts(connection->accumulation, connection->accumulation_length);
    status = sl_http_request_parse_head(&connection->request, full_message, out_diag);
    if (!sl_status_is_ok(status)) {
        if (out_diag != NULL) {
            sl_http_transport_store_diag(connection, out_diag);
        }
        return status;
    }

    if (sl_http_transport_has_header(&connection->request.head,
                                     sl_str_from_cstr("Transfer-Encoding")))
    {
        (void)sl_http_request_fail(&connection->request, NULL);
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_UNSUPPORTED_BODY, SL_STATUS_UNSUPPORTED,
            sl_http_transport_literal("HTTP transfer encoding is not supported",
                                      sizeof("HTTP transfer encoding is not supported") - 1U),
            sl_http_transport_literal("send a bounded Content-Length body",
                                      sizeof("send a bounded Content-Length body") - 1U));
    }

    if (!sl_http_transport_header_value(&connection->request.head, sl_str_from_cstr("Content-Type"),
                                        &content_type))
    {
        content_type = sl_str_empty();
    }
    if (sl_http_transport_header_value(&connection->request.head,
                                       sl_str_from_cstr("Content-Length"), &content_length) &&
        !sl_http_transport_parse_size_decimal(content_length, &connection->expected_body_length))
    {
        (void)sl_http_request_fail(&connection->request, NULL);
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP Content-Length is invalid",
                                      sizeof("HTTP Content-Length is invalid") - 1U),
            sl_http_transport_literal("Content-Length must be a decimal byte count",
                                      sizeof("Content-Length must be a decimal byte count") - 1U));
    }

    status = sl_http_request_body_reader_begin(&connection->request, content_type,
                                               connection->expected_body_length,
                                               &connection->body_reader, out_diag);
    if (!sl_status_is_ok(status)) {
        if (out_diag != NULL) {
            sl_http_transport_store_diag(connection, out_diag);
        }
        return status;
    }
    connection->body_reader_started = true;

    body = sl_bytes_from_parts(connection->accumulation + connection->head_length,
                               connection->expected_body_length);
    status = sl_http_request_body_reader_append(&connection->body_reader, body, out_diag);
    if (!sl_status_is_ok(status)) {
        if (out_diag != NULL) {
            sl_http_transport_store_diag(connection, out_diag);
        }
        return status;
    }
    status = sl_http_request_body_reader_finish(&connection->body_reader, out_diag);
    if (!sl_status_is_ok(status)) {
        if (out_diag != NULL) {
            sl_http_transport_store_diag(connection, out_diag);
        }
        return status;
    }
    connection->body_reader_finished = true;
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_REQUEST_READY;
    if (connection->platform != NULL) {
        connection->platform->reading = false;
        (void)uv_read_stop((uv_stream_t*)&connection->platform->handle);
    }
    if (connection->core.backend != NULL && connection->core.backend->listener.platform != NULL) {
        SlHttpTransportServer* server =
            ((SlHttpPlatformListener*)connection->core.backend->listener.platform)->server;
        if (server != NULL) {
            if (server->config.on_request_ready != NULL) {
                server->config.on_request_ready(connection, &connection->request,
                                                server->config.on_request_ready_user);
            }
            if (server->config.dispatch != NULL) {
                return sl_http_transport_dispatch_ready(connection, server, out_diag);
            }
            if (server->config.on_request_ready == NULL) {
                (void)sl_http_transport_connection_close(connection, NULL);
            }
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_try_complete_request(SlHttpTransportConnection* connection,
                                                       SlDiag* out_diag)
{
    SlHttpTransportServer* server = NULL;
    size_t total_needed = 0U;
    SlStatus status;

    if (connection == NULL || connection->core.backend == NULL ||
        connection->core.backend->listener.platform == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    server = ((SlHttpPlatformListener*)connection->core.backend->listener.platform)->server;
    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (connection->head_length == 0U) {
        connection->head_length = sl_http_transport_find_header_end(
            sl_bytes_from_parts(connection->accumulation, connection->accumulation_length));
        if (connection->head_length == 0U) {
            if (connection->accumulation_length > server->config.max_request_head_bytes) {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_HTTP_HEADER_BYTES_LIMIT,
                    SL_STATUS_CAPACITY_EXCEEDED,
                    sl_http_transport_literal("HTTP request head is too large",
                                              sizeof("HTTP request head is too large") - 1U),
                    sl_http_transport_literal(
                        "the transport rejects request heads above the configured cap",
                        sizeof("the transport rejects request heads above the configured cap") -
                            1U));
            }
            connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
            return sl_status_ok();
        }
        if (connection->head_length > server->config.max_request_head_bytes) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_HEADER_BYTES_LIMIT, SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP request head is too large",
                                          sizeof("HTTP request head is too large") - 1U),
                sl_http_transport_literal(
                    "the transport rejects request heads above the configured cap",
                    sizeof("the transport rejects request heads above the configured cap") - 1U));
        }
        sl_http_transport_stop_timer(&connection->platform->header_timer,
                                     &connection->platform->header_timer_initialized);
    }

    if (sl_http_transport_head_header_value(
            sl_bytes_from_parts(connection->accumulation, connection->head_length),
            sl_str_from_cstr("Transfer-Encoding"), NULL, NULL))
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_UNSUPPORTED_BODY, SL_STATUS_UNSUPPORTED,
            sl_http_transport_literal("HTTP transfer encoding is not supported",
                                      sizeof("HTTP transfer encoding is not supported") - 1U),
            sl_http_transport_literal("send a bounded Content-Length body",
                                      sizeof("send a bounded Content-Length body") - 1U));
    }

    {
        SlStr content_length = {0};
        size_t content_length_count = 0U;
        if (sl_http_transport_head_header_value(
                sl_bytes_from_parts(connection->accumulation, connection->head_length),
                sl_str_from_cstr("Content-Length"), &content_length, &content_length_count))
        {
            if (content_length_count != 1U ||
                !sl_http_transport_parse_size_decimal(content_length,
                                                      &connection->expected_body_length))
            {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
                    sl_http_transport_literal("HTTP Content-Length is invalid",
                                              sizeof("HTTP Content-Length is invalid") - 1U),
                    sl_http_transport_literal("Content-Length must be a decimal byte count",
                                              sizeof("Content-Length must be a decimal byte "
                                                     "count") -
                                                  1U));
            }
        }
    }
    if (connection->expected_body_length > server->backend.options.parse.max_body_length) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_BODY_LIMIT, SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP request body is too large",
                                      sizeof("HTTP request body is too large") - 1U),
            sl_http_transport_literal("request bodies are copied only up to the configured limit",
                                      sizeof("request bodies are copied only up to the configured "
                                             "limit") -
                                          1U));
    }

    status = sl_checked_add_size(connection->head_length, connection->expected_body_length,
                                 &total_needed);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (connection->accumulation_length < total_needed) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY;
        status = sl_http_transport_start_timer(
            connection->platform, &connection->platform->body_timer,
            &connection->platform->body_timer_initialized, server->config.body_read_timeout_ms,
            sl_http_transport_body_timeout_cb);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
                sl_http_transport_literal("HTTP transport body timeout could not start",
                                          sizeof("HTTP transport body timeout could not start") -
                                              1U),
                sl_http_transport_literal("the connection will be closed",
                                          sizeof("the connection will be closed") - 1U));
        }
        return sl_status_ok();
    }
    if (connection->accumulation_length > total_needed) {
        (void)sl_http_request_fail(&connection->request, NULL);
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED, SL_STATUS_UNSUPPORTED,
            sl_http_transport_literal("HTTP pipelining is not supported",
                                      sizeof("HTTP pipelining is not supported") - 1U),
            sl_http_transport_literal("send one request per connection for the MVP transport",
                                      sizeof("send one request per connection for the MVP "
                                             "transport") -
                                          1U));
    }

    sl_http_transport_stop_timer(&connection->platform->body_timer,
                                 &connection->platform->body_timer_initialized);
    return sl_http_transport_parse_accumulated(connection, out_diag);
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
            SlHttpPlatformConnection* platform = connection->platform;
            unsigned char* request_storage = connection->request_storage;
            size_t request_storage_size = connection->request_storage_size;
            unsigned char* accumulation = connection->accumulation;
            size_t accumulation_capacity = connection->accumulation_capacity;
            unsigned char* response_storage = connection->response_storage;
            size_t response_storage_size = connection->response_storage_size;
            unsigned char* read_buffer = platform == NULL ? NULL : platform->read_buffer;
            size_t read_buffer_size = platform == NULL ? 0U : platform->read_buffer_size;
            *connection = (SlHttpTransportConnection){0};
            connection->platform = platform;
            connection->request_storage = request_storage;
            connection->request_storage_size = request_storage_size;
            connection->accumulation = accumulation;
            connection->accumulation_capacity = accumulation_capacity;
            connection->response_storage = response_storage;
            connection->response_storage_size = response_storage_size;
            connection->platform->owner = connection;
            connection->platform->read_buffer = read_buffer;
            connection->platform->read_buffer_size = read_buffer_size;
            (void)sl_arena_init(&connection->request_arena, connection->request_storage,
                                connection->request_storage_size);
            (void)sl_byte_builder_init_fixed(&connection->accumulation_builder,
                                             connection->accumulation,
                                             connection->accumulation_capacity);
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

static void sl_http_transport_alloc_read(uv_handle_t* handle, size_t suggested_size,
                                         uv_buf_t* out_buffer)
{
    SlHttpPlatformConnection* platform =
        handle == NULL ? NULL : (SlHttpPlatformConnection*)handle->data;

    (void)suggested_size;
    if (out_buffer == NULL) {
        return;
    }
    if (platform == NULL || platform->read_buffer == NULL || platform->read_buffer_size == 0U) {
        *out_buffer = uv_buf_init(NULL, 0U);
        return;
    }
    *out_buffer =
        uv_buf_init((char*)platform->read_buffer, (unsigned int)platform->read_buffer_size);
}

static void sl_http_transport_fail_and_close(SlHttpTransportConnection* connection, SlDiag* diag)
{
    uint16_t status_code = 500U;
    SlDiag write_diag = {0};

    if (connection == NULL) {
        return;
    }
    sl_http_transport_store_diag(connection, diag);
    if (connection->request_started && connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_FAILED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT)
    {
        (void)sl_http_request_fail(&connection->request, NULL);
    }
    if (connection->platform != NULL && connection->platform->initialized &&
        !connection->platform->closing && !connection->platform->writing &&
        connection->response_storage != NULL && connection->response_storage_size != 0U)
    {
        status_code = sl_http_transport_status_for_failure(
            sl_status_from_code(diag == NULL ? SL_STATUS_INTERNAL : SL_STATUS_INVALID_ARGUMENT),
            diag);
        if (sl_status_is_ok(
                sl_http_transport_write_error_response(connection, status_code, &write_diag)))
        {
            return;
        }
    }
    (void)sl_http_transport_connection_close(connection, NULL);
}

static void sl_http_transport_on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buffer)
{
    SlHttpPlatformConnection* platform =
        stream == NULL ? NULL : (SlHttpPlatformConnection*)stream->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlDiag diag = {0};
    SlStatus status;

    (void)buffer;
    if (connection == NULL) {
        return;
    }
    if (nread > 0) {
        status = sl_http_transport_connection_feed_test(
            connection, sl_bytes_from_parts(platform->read_buffer, (size_t)nread), &diag);
        if (!sl_status_is_ok(status)) {
            sl_http_transport_fail_and_close(connection, &diag);
        }
        return;
    }
    if (nread == 0) {
        return;
    }

    if (nread == UV_EOF) {
        SlDiagCode code = SL_DIAG_HTTP_CONNECTION_CLOSED;
        SlStatusCode status_code = SL_STATUS_CANCELLED;
        SlStr message = connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY
                            ? sl_http_transport_literal(
                                  "HTTP client disconnected during request body",
                                  sizeof("HTTP client disconnected during request body") - 1U)
                            : sl_http_transport_literal(
                                  "HTTP client disconnected during request head",
                                  sizeof("HTTP client disconnected during request head") - 1U);
        status = sl_http_transport_connection_diag(
            connection, &diag, code, status_code, message,
            sl_http_transport_literal("request cleanup still runs after disconnect",
                                      sizeof("request cleanup still runs after disconnect") - 1U));
        (void)status;
        if (connection->request_started &&
            connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_COMPLETED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT &&
            connection->request.state != SL_HTTP_REQUEST_STATE_FAILED)
        {
            (void)sl_http_request_cancel(
                &connection->request, SL_CANCELLATION_REASON_CANCELLED,
                sl_http_transport_literal("HTTP client disconnected",
                                          sizeof("HTTP client disconnected") - 1U),
                NULL);
        }
        (void)sl_http_transport_connection_close(connection, NULL);
        return;
    }

    status = sl_http_transport_connection_diag(
        connection, &diag, SL_DIAG_HTTP_CONNECTION_CLOSED, SL_STATUS_INTERNAL,
        sl_http_transport_literal("HTTP transport read failed",
                                  sizeof("HTTP transport read failed") - 1U),
        sl_http_transport_literal("socket details stay inside the platform boundary",
                                  sizeof("socket details stay inside the platform boundary") - 1U));
    (void)status;
    sl_http_transport_fail_and_close(connection, &diag);
}

static SlStatus sl_http_transport_start_read(SlHttpTransportConnection* connection,
                                             SlDiag* out_diag)
{
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
    SlStatus status;
    int rc = 0;

    if (connection == NULL || connection->platform == NULL || !connection->platform->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport connection state is invalid",
                                      sizeof("HTTP transport connection state is invalid") - 1U),
            sl_http_transport_literal("start reads only after a connection is accepted",
                                      sizeof("start reads only after a connection is accepted") -
                                          1U));
    }
    rc = uv_read_start((uv_stream_t*)&connection->platform->handle, sl_http_transport_alloc_read,
                       sl_http_transport_on_read);
    if (rc != 0) {
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_CONNECTION_CLOSED,
            sl_http_transport_literal("HTTP transport read start failed",
                                      sizeof("HTTP transport read start failed") - 1U));
    }
    connection->platform->reading = true;
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
    if (server != NULL) {
        status = sl_http_transport_start_timer(
            connection->platform, &connection->platform->header_timer,
            &connection->platform->header_timer_initialized, server->config.header_read_timeout_ms,
            sl_http_transport_header_timeout_cb);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
                sl_http_transport_literal("HTTP transport header timeout could not start",
                                          sizeof("HTTP transport header timeout could not start") -
                                              1U),
                sl_http_transport_literal("the connection will be closed",
                                          sizeof("the connection will be closed") - 1U));
        }
        status = sl_http_transport_start_timer(
            connection->platform, &connection->platform->request_timer,
            &connection->platform->request_timer_initialized, server->config.request_timeout_ms,
            sl_http_transport_request_timeout_cb);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
                sl_http_transport_literal("HTTP transport request timeout could not start",
                                          sizeof("HTTP transport request timeout could not start") -
                                              1U),
                sl_http_transport_literal("the connection will be closed",
                                          sizeof("the connection will be closed") - 1U));
        }
    }
    return sl_status_ok();
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
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED ||
        server->backend.state == SL_HTTP_BACKEND_STATE_STOPPING ||
        server->backend.state == SL_HTTP_BACKEND_STATE_STOPPED)
    {
        server->rejected_connections += 1U;
        sl_http_transport_reject_pending(listener, platform);
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
    if (!sl_status_is_ok(sl_http_transport_start_read(connection, NULL))) {
        (void)sl_http_connection_fail(&connection->core, NULL);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        (void)sl_http_transport_connection_close(connection, NULL);
        server->accept_failures += 1U;
    }
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
    size_t accumulation_capacity = 0U;
    size_t accumulation_bytes = 0U;
    size_t request_storage_bytes = 0U;
    size_t read_buffer_bytes = 0U;
    size_t response_storage_bytes = 0U;

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
    backend_options.parse = server->config.parse;
    backend_options.read_timeout_ms = server->config.body_read_timeout_ms;
    backend_options.header_timeout_ms = server->config.header_read_timeout_ms;
    backend_options.request_timeout_ms = server->config.request_timeout_ms;
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

    status =
        sl_checked_add_size(server->config.max_request_head_bytes,
                            server->backend.options.parse.max_body_length, &accumulation_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(accumulation_capacity, server->connection_capacity,
                                 &accumulation_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(server->config.request_arena_bytes, server->connection_capacity,
                                 &request_storage_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(server->config.read_chunk_bytes, server->connection_capacity,
                                 &read_buffer_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(server->config.max_response_bytes, server->connection_capacity,
                                 &response_storage_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    (void)accumulation_bytes;
    (void)request_storage_bytes;
    (void)read_buffer_bytes;
    (void)response_storage_bytes;

    for (size_t index = 0U; index < server->connection_capacity; index += 1U) {
        server->connections[index] = (SlHttpTransportConnection){0};
        server->connections[index].platform = &server->platform_connections[index];
        if (accumulation_capacity != 0U) {
            status = sl_http_transport_alloc(arena, accumulation_capacity, 1U, &memory);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            server->connections[index].accumulation = (unsigned char*)memory;
            server->connections[index].accumulation_capacity = accumulation_capacity;
        }
        status = sl_http_transport_alloc(arena, server->config.max_response_bytes, 1U, &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        server->connections[index].response_storage = (unsigned char*)memory;
        server->connections[index].response_storage_size = server->config.max_response_bytes;
        status = sl_http_transport_alloc(arena, server->config.request_arena_bytes, 1U, &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        server->connections[index].request_storage = (unsigned char*)memory;
        server->connections[index].request_storage_size = server->config.request_arena_bytes;
        server->platform_connections[index] = (SlHttpPlatformConnection){0};
        status = sl_http_transport_alloc(arena, server->config.read_chunk_bytes, 1U, &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        server->platform_connections[index].read_buffer = (unsigned char*)memory;
        server->platform_connections[index].read_buffer_size = server->config.read_chunk_bytes;
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

SlStatus sl_http_transport_server_run(SlHttpTransportServer* server, SlDiag* out_diag)
{
    int rc = 0;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || server->platform == NULL || !server->platform->loop_initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING)
    {
        return sl_http_transport_invalid_state(out_diag);
    }

    rc = uv_run(&server->platform->loop, UV_RUN_DEFAULT);
    if (rc != 0 || server->accept_failures != 0U) {
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_ACCEPT_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP transport run failed",
                                      sizeof("HTTP transport run failed") - 1U),
            sl_http_transport_literal("libuv details stay inside the platform transport boundary",
                                      sizeof("libuv details stay inside the platform transport "
                                             "boundary") -
                                          1U));
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
    if (connection->platform != NULL && connection->platform->initialized &&
        connection->platform->reading)
    {
        (void)uv_read_stop((uv_stream_t*)&connection->platform->handle);
        connection->platform->reading = false;
    }
    if (connection->platform != NULL && connection->platform->writing) {
        sl_http_transport_close_timer(&connection->platform->header_timer,
                                      &connection->platform->header_timer_initialized);
        sl_http_transport_close_timer(&connection->platform->body_timer,
                                      &connection->platform->body_timer_initialized);
        sl_http_transport_close_timer(&connection->platform->request_timer,
                                      &connection->platform->request_timer_initialized);
        if (connection->request_started &&
            !sl_http_transport_request_terminal(connection->request.state))
        {
            (void)sl_http_request_shutdown(&connection->request, NULL);
        }
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING;
        return sl_status_ok();
    }
    sl_http_transport_close_connection_timers(connection->platform);
    if (connection->body_reader_started && !connection->body_reader_finished) {
        (void)sl_http_request_body_reader_close(&connection->body_reader, NULL);
    }
    if (connection->request_started) {
        if (connection->core.backend != NULL &&
            connection->core.backend->state == SL_HTTP_BACKEND_STATE_STOPPING &&
            connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_COMPLETED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT &&
            connection->request.state != SL_HTTP_REQUEST_STATE_FAILED)
        {
            (void)sl_http_request_shutdown(&connection->request, NULL);
        }
        else {
            (void)sl_http_request_close(&connection->request, NULL);
        }
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

SlStatus sl_http_transport_connection_feed_test(SlHttpTransportConnection* connection,
                                                SlBytes bytes, SlDiag* out_diag)
{
    SlStatus status;

    sl_http_transport_clear_diag(out_diag);
    if (connection == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED &&
        connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD &&
        connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport connection state is invalid",
                                      sizeof("HTTP transport connection state is invalid") - 1U),
            sl_http_transport_literal(
                "request bytes can be read only before request-ready",
                sizeof("request bytes can be read only before request-ready") - 1U));
    }
    if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
    }

    status = sl_http_transport_append_bytes(connection, bytes, out_diag);
    if (!sl_status_is_ok(status)) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        return status;
    }
    status = sl_http_transport_try_complete_request(connection, out_diag);
    if (!sl_status_is_ok(status)) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        return status;
    }
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
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_ERROR)
    {
        return sl_http_transport_invalid_state(out_diag);
    }

    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING;
    if (server->backend.state == SL_HTTP_BACKEND_STATE_STARTED ||
        server->backend.state == SL_HTTP_BACKEND_STATE_STOPPING)
    {
        status = sl_http_backend_stop(&server->backend, out_diag);
    }

    for (size_t index = 0U; index < server->connection_capacity; index += 1U) {
        (void)sl_http_transport_connection_close(&server->connections[index], NULL);
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
